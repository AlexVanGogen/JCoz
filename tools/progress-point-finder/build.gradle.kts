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
    implementation("org.ow2.asm:asm:7.1")
}

tasks.jar {
    manifest {
        attributes("Premain-Class" to "edu.avgogen.jcoz.tools.ppfinder.ProgressPointFinderAgent")
    }
}

task<JavaExec>("testFinder") {
    dependsOn += tasks["build"]
    classpath = sourceSets["test"].runtimeClasspath
    main = "edu.avgogen.jcoz.tools.ppfinder.ProgressPointFinderTestKt"
    jvmArgs = listOf("-javaagent:${tasks.jar.get().archiveFile.get()}")
}