package edu.avgogen.jcoz.tools.ppfinder

import org.objectweb.asm.*
import org.objectweb.asm.ClassWriter.COMPUTE_MAXS
import org.objectweb.asm.Opcodes.ASM7
import java.lang.instrument.ClassFileTransformer
import java.security.ProtectionDomain

class SourceLineHitsDetector : ClassFileTransformer {

    override fun transform(
        loader: ClassLoader?,
        className: String?,
        classBeingRedefined: Class<*>?,
        protectionDomain: ProtectionDomain?,
        classfileBuffer: ByteArray?
    ) : ByteArray {
        val reader = ClassReader(classfileBuffer)
        val writer = ClassWriter(reader, COMPUTE_MAXS)
        reader.accept(ClassSourceLineHitsRecordingVisitor(writer, ASM7), 0)
        return writer.toByteArray()
    }

    class ClassSourceLineHitsRecordingVisitor(val classVisitor: ClassVisitor, val opcode: Int) : ClassVisitor(opcode, classVisitor) {

        override fun visitMethod(
            access: Int,
            name: String?,
            desc: String?,
            signature: String?,
            exceptions: Array<out String>?
        ): MethodVisitor? {
            println(
                """
            Method $name:
                access: $access
                description: $desc
                signature: $signature
                exceptions: ${if (exceptions?.isNotEmpty() == true) exceptions.joinToString() else "none"}
                """.trimIndent()
            )
            return PerMethodSourceLineHitsRecordingVisitor(
                classVisitor.visitMethod(access, name, desc, signature, exceptions),
                opcode
            )
        }
    }

    class PerMethodSourceLineHitsRecordingVisitor(val methodVisitor: MethodVisitor, opcode: Int) : MethodVisitor(opcode, methodVisitor) {

        override fun visitLineNumber(line: Int, start: Label?) {
            println("Line number $line")
            methodVisitor.visitLineNumber(line, start)
        }
    }
}
