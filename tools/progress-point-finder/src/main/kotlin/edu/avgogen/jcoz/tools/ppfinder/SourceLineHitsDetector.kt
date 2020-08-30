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
        classfileBuffer: ByteArray
    ): ByteArray? {
        if (loader == null || classBeingRedefined != null || className == null || className.startsWith("edu/avgogen/jcoz/tools/ppfinder/HitsRecorder")) {
            return null
        }
        val reader = ClassReader(classfileBuffer)
        val writer = ClassWriter(COMPUTE_MAXS)
        reader.accept(ClassSourceLineHitsRecordingVisitor(writer, className, ASM7), 0)
        return writer.toByteArray()
    }

    class ClassSourceLineHitsRecordingVisitor(
        classVisitor: ClassVisitor,
        private val className: String,
        private val opcode: Int
    ) : ClassVisitor(opcode, classVisitor) {

        override fun visitMethod(
            access: Int,
            name: String,
            desc: String,
            signature: String?,
            exceptions: Array<out String>?
        ): MethodVisitor? {
            return PerMethodSourceLineHitsRecordingVisitor(
                    cv.visitMethod(access, name, desc, signature, exceptions),
                    className,
                    opcode
            )
        }
    }

    class PerMethodSourceLineHitsRecordingVisitor(
        methodVisitor: MethodVisitor,
        private val className: String,
        opcode: Int
    ) : MethodVisitor(opcode, methodVisitor) {

        private var lastLine = -1

        override fun visitLineNumber(line: Int, start: Label?) {
            if (line != lastLine) {
                mv.visitLdcInsn(className)
                mv.visitLdcInsn(line)
                mv.visitMethodInsn(INVOKESTATIC, "edu/avgogen/jcoz/tools/ppfinder/HitsRecorder", "registerHit", "(Ljava/lang/String;I)V", false)
                lastLine = line
            }
            mv.visitLineNumber(line, start)
        }
    }
}
