package edu.avgogen.jcoz.tools.ppfinder

fun foo() {
    println("Haha!")
}

fun main() {
    foo().also {
        foo()
    }
}