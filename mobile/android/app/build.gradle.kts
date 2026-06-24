plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "studio.tin.fitnessai"
    compileSdk = 34

    defaultConfig {
        applicationId = "studio.tin.fitnessai"
        minSdk = 26                 // Android 8.0 — needed for foreground services + step counter
        targetSdk = 34
        versionCode = 1             // KEEP IN SYNC with mobile/version.json -> androidApp.versionCode
        versionName = "0.1.0"       // KEEP IN SYNC with mobile/version.json -> androidApp.version

        // The app reads this to know where to POST data and where to check for updates.
        buildConfigField("String", "BACKEND_URL", "\"https://pymite6941-data-analyst-ai-agent.hf.space\"")
        buildConfigField("String", "VERSION_JSON_URL",
            "\"https://raw.githubusercontent.com/PyMite6941/Fitness-AI-Agents/main/mobile/version.json\"")
    }

    buildFeatures { buildConfig = true; viewBinding = true }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.work:work-runtime-ktx:2.9.1")          // background upload retries
    implementation("androidx.security:security-crypto:1.1.0-alpha06") // EncryptedSharedPreferences
    implementation("com.google.android.gms:play-services-location:21.3.0") // FusedLocation (GPS)
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
}
