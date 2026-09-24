plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

// La llave de firma llega del CI (secretos ANDROID_LLAVE y ANDROID_LLAVE_CLAVE,
// ver .github/workflows/movil.yml). Con ella cada versión se instala encima de
// la anterior. Sin ella (en tu PC, por ejemplo) se firma con la de depuración.
val llaveArchivo: String? = System.getenv("SOKARI_LLAVE_ARCHIVO")
val llaveClave: String? = System.getenv("SOKARI_LLAVE_CLAVE")
val conLlave = !llaveArchivo.isNullOrEmpty() && !llaveClave.isNullOrEmpty()

android {
    namespace = "com.podsel.sokari_remoto"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        applicationId = "com.podsel.sokari_remoto"
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        // Del pubspec, o de --build-name/--build-number (el release usa la versión de Sokari).
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    signingConfigs {
        if (conLlave) {
            create("sokari") {
                storeFile = file(llaveArchivo!!)
                storeType = "pkcs12"
                storePassword = llaveClave
                keyAlias = "sokari"
                keyPassword = llaveClave
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.getByName(if (conLlave) "sokari" else "debug")
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}
