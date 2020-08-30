package edu.avgogen.jcoz.tools.ppfinder

import java.lang.instrument.Instrumentation

class ProgressPointFinderAgent {

    companion object {
        @JvmStatic
        fun premain(agentArgs: String?, instrumentation: Instrumentation) {
            val b = with(HitsRecorder().javaClass) {
                classLoader.getResourceAsStream(name.replace('.', '/') + ".class")
            }.use {
                it?.readAllBytes()
            } ?: throw AssertionError()
            CustomClassLoader().defineClass("edu.avgogen.jcoz.tools.ppfinder.HitsRecorder", b)
            instrumentation.addTransformer(SourceLineHitsDetector())
        }
    }
}