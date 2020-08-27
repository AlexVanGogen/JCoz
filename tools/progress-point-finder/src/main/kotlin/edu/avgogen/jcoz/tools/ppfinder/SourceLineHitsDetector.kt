package edu.avgogen.jcoz.tools.ppfinder

import org.objectweb.asm.*
import org.objectweb.asm.ClassWriter.COMPUTE_MAXS
import org.objectweb.asm.Opcodes.*
import java.lang.instrument.ClassFileTransformer
import java.security.ProtectionDomain

class SourceLineHitsDetector : ClassFileTransformer {

    override fun transform(
        loader: ClassLoader?,
        className: String?,
        classBeingRedefined: Class<*>?,
        protectionDomain: ProtectionDomain?,
        classfileBuffer: ByteArray?
    ): ByteArray {
        val reader = ClassReader(classfileBuffer)
        val writer = ClassWriter(reader, COMPUTE_MAXS)
        reader.accept(ClassSourceLineHitsRecordingVisitor(writer, className, ASM7), 0)
        return writer.toByteArray()
    }

    class ClassSourceLineHitsRecordingVisitor(
        private val classVisitor: ClassVisitor,
        private val className: String?,
        private val opcode: Int
    ) : ClassVisitor(opcode, classVisitor) {

        override fun visitMethod(
            access: Int,
            name: String,
            desc: String,
            signature: String?,
            exceptions: Array<out String>?
        ): MethodVisitor? {

            return if (name.contains("foo") || name.contains("bar")) {
                PerMethodSourceLineHitsRecordingVisitor(
                    classVisitor.visitMethod(access, name, desc, signature, exceptions),
                    className,
                    opcode
                )
            } else classVisitor.visitMethod(access, name, desc, signature, exceptions)
        }
    }

    class PerMethodSourceLineHitsRecordingVisitor(
        private val methodVisitor: MethodVisitor,
        private val className: String?,
        opcode: Int
    ) : MethodVisitor(opcode, methodVisitor) {

        private var lastLine = -1

        override fun visitLineNumber(line: Int, start: Label?) {
            if (line != lastLine) {
                interceptDumpLine(line)
                interceptTimestamp()
                lastLine = line
            }
            methodVisitor.visitLineNumber(line, start)
        }

        private fun interceptDumpLine(lineNumber: Int) {
            interceptPrint("(Ljava/lang/String;)V", false) {
                visitLdcInsn("$className:$lineNumber: ")
            }
        }

        private fun interceptTimestamp() {
            interceptPrint("(J)V", true) {
                visitMethodInsn(INVOKESTATIC, "java/lang/System", "nanoTime", "()J", false)
            }
        }

        private inline fun interceptPrint(descriptor: String, newLine: Boolean, insns: MethodVisitor.() -> Unit) {
            visitFieldInsn(GETSTATIC, "java/lang/System", "out", "Ljava/io/PrintStream;")
            this.insns()
            visitMethodInsn(
                INVOKEVIRTUAL,
                "java/io/PrintStream",
                if (newLine) "println" else "print",
                descriptor,
                false
            )
        }
    }
}
