#ifndef SOKARI_AGENT_H
#define SOKARI_AGENT_H

#include <stdbool.h>

typedef struct Conversation Conversation;

typedef struct {
    char *reply;     /* lo que hay que decir (heap), o NULL */
    bool keep_going; /* ¿sigue abierta la conversación? */
    bool shutdown;   /* se dijo la palabra de apagado */
} TurnResult;

void agent_init(void);
Conversation *conv_create(bool load_recent_memory);
void conv_destroy(Conversation *c);
void conv_new_session(Conversation *c);
/* Conversación que llega por la malla: las acciones que piden confirmación
   de voz se niegan en vez de quedar esperando un "sí". */
void conv_set_remote(Conversation *c, bool remote);

typedef enum { ANSWER_OTHER, ANSWER_YES, ANSWER_NO } AgentAnswer;
AgentAnswer agent_classify_answer(const char *text);

/* Pipeline completo para un texto ya transcripto (o llegado por la malla):
   palabra de apagado (chequeo local, nunca llega a Groq) -> despedida ->
   modelo + herramientas. Llamar con state_lock() tomado. */
TurnResult agent_process(Conversation *c, const char *text);

#endif
