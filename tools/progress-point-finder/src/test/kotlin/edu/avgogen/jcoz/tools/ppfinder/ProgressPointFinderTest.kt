package edu.avgogen.jcoz.tools.ppfinder

fun foo() {
    println(Obj.bar(42))
    println(81)
}

object Obj {
    fun bar(x: Int): Int {
        return x * 2
    }
}

fun main() {
    foo()
}