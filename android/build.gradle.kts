plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.compose.compiler) apply false
    alias(libs.plugins.ksp) apply false
    alias(libs.plugins.protobuf) apply false
}

// AarMetadata check tasks are intentionally left enabled: lint's
// bundleDebugLocalLintAar consumes the checkDebugAarMetadata output directory,
// so blanket-disabling them breaks lintDebug with an AGP "input file does not
// exist" failure (GitHub Actions run 40741542355).

subprojects {
    afterEvaluate {
        val bundleTask = tasks.findByName("bundleDebugLocalLintAar")
        val checkTask = tasks.findByName("checkDebugAarMetadata")
        if (bundleTask != null && checkTask != null) {
            bundleTask.dependsOn(checkTask)
        }
    }
}
