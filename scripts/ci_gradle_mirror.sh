#!/usr/bin/env bash
# Makes Gradle look in Google's mirror of Maven Central before the real one, for the CI only.
# Maven Central answers "429 Too Many Requests" to the shared addresses of the runners; the mirror
# serves the same files and does not. The Android template that a game gets is not changed.
set -eu

init_dir="${GRADLE_USER_HOME:-$HOME/.gradle}/init.d"
mkdir -p "$init_dir"
cat >"$init_dir/ci-mirror.gradle" <<'GRADLE'
def mirror = 'https://maven-central.storage-download.googleapis.com/maven2/'
beforeSettings { settings ->
    settings.pluginManagement.repositories {
        maven { url mirror }
    }
    settings.dependencyResolutionManagement.repositories {
        maven { url mirror }
    }
}
GRADLE
echo "Gradle will try the Maven Central mirror first ($init_dir/ci-mirror.gradle)"
