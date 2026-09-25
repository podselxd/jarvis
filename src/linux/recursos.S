/* Los archivos de res/ adentro del ejecutable de Linux, como el .rc los mete
   en el .exe. Se ensambla con -I res (y -DHAVE_HEY_SOKARI si el modelo
   existe). */
    .section .rodata
    .balign 16
    .global sokari_res_tools, sokari_res_tools_end
sokari_res_tools:
    .incbin "tools.json"
sokari_res_tools_end:
    .balign 16
    .global sokari_res_prompt, sokari_res_prompt_end
sokari_res_prompt:
    .incbin "system_prompt.txt"
sokari_res_prompt_end:
    .balign 16
    .global sokari_res_wakeword, sokari_res_wakeword_end
sokari_res_wakeword:
    .incbin "wakeword.bin"
sokari_res_wakeword_end:
    .balign 16
    .global sokari_res_hey, sokari_res_hey_end
sokari_res_hey:
#ifdef HAVE_HEY_SOKARI
    .incbin "hey_sokari.jww"
#endif
sokari_res_hey_end:
    .section .note.GNU-stack,"",@progbits
