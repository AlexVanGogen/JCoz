plugins {
    kotlin("jvm") version "1.4.0"
}

group = "edu.avgogen"
version = "1.0-SNAPSHOT"

repositories {
    mavenCentral()
}

dependencies {
    implementation(kotlin("stdlib"))
}

tasks.jar {
    manifest {
        attributes("Premain-Class" to "edu.avgogen.jcoz.tools.ppfinder.ProgressPointFinderAgent")
    }
}