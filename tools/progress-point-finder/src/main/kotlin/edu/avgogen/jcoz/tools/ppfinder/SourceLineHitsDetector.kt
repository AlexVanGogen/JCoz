package edu.avgogen.jcoz.tools.ppfinder

import org.objectweb.asm.*
import org.objectweb.asm.ClassWriter.COMPUTE_MAXS
import org.objectweb.asm.Opcodes.*
import java.lang.instrument.ClassFileTransformer
import java.security.ProtectionDomain

class SourceLineHitsDetector(private val outputFileName: String) : ClassFileTransformer {

    override fun transform(
        loader: ClassLoader?,
        className: String?,
        classBeingRedefined: Class<*>?,
        protectionDomain: ProtectionDomain?,
        classfileBuffer: ByteArray
    ): ByteArray? {
        if (loader == null || classBeingRedefined != null) {
            return null
        }
        val reader = ClassReader(classfileBuffer)
        val writer = ClassWriter(reader, COMPUTE_MAXS)
        reader.accept(ClassSourceLineHitsRecordingVisitor(writer, className, ASM7, outputFileName), 0)
        return writer.toByteArray()
    }

    class ClassSourceLineHitsRecordingVisitor(
        private val classVisitor: ClassVisitor,
        private val className: String?,
        private val opcode: Int,
        private val outputFileName: String
    ) : ClassVisitor(opcode, classVisitor) {

        override fun visitMethod(
            access: Int,
            name: String,
            desc: String,
            signature: String?,
            exceptions: Array<out String>?
        ): MethodVisitor? {
            return PerMethodSourceLineHitsRecordingVisitor(
                classVisitor.visitMethod(access, name, desc, signature, exceptions),
                className,
                opcode,
                outputFileName
            )
        }
    }

    class PerMethodSourceLineHitsRecordingVisitor(
        private val methodVisitor: MethodVisitor,
        private val className: String?,
        opcode: Int,
        private val outputFileName: String
    ) : MethodVisitor(opcode, methodVisitor) {

        private var lastLine = -1

        override fun visitLineNumber(line: Int, start: Label?) {
            if (line != lastLine) {
                interceptDumpLine(outputFileName, line)
                interceptTimestamp(outputFileName)
                lastLine = line
            }
            methodVisitor.visitLineNumber(line, start)
        }

        private fun interceptDumpLine(fileName: String, lineNumber: Int) {
            initFileAndWriteText(fileName) {
                visitLdcInsn("\n$className:$lineNumber: ")
            }
        }

        private fun interceptTimestamp(fileName: String) {
            initFileAndWriteText(fileName) {
                visitMethodInsn(INVOKESTATIC, "java/lang/System", "nanoTime", "()J", false)
                visitMethodInsn(INVOKESTATIC, "java/lang/String", "valueOf", "(J)Ljava/lang/String;", false)
            }
        }

        private inline fun initFileAndWriteText(fileName: String, buildTextBytecode: MethodVisitor.() -> Unit) {
            // File(fileName)
            visitTypeInsn(NEW, "java/io/File")
            visitInsn(DUP)
            visitLdcInsn(fileName)
            visitMethodInsn(INVOKESPECIAL, "java/io/File", "<init>", "(Ljava/lang/String;)V", false)

            // .appendText(text)
            this.buildTextBytecode()

            visitInsn(ACONST_NULL)
            visitInsn(ICONST_2)
            visitInsn(ACONST_NULL)
            visitMethodInsn(INVOKESTATIC, "kotlin/io/FilesKt", "appendText\$default", "(Ljava/io/File;Ljava/lang/String;Ljava/nio/charset/Charset;ILjava/lang/Object;)V", false)
        }
    }
}
