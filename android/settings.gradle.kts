pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "dashcue"

include(":app")
include(":core:common")
include(":core:protocol")
include(":core:ble")
include(":core:database")
include(":feature:media")
include(":feature:navigation")
include(":feature:onboarding")
include(":feature:settings")
include(":feature:diagnostics")
