package edu.avgogen.jcoz.tools.ppfinder

import java.io.File

class A(val x: Int) {

}

interface Fooable {
    fun foo(x: String)
}

fun foo(a: A) {
//    if (a.x > 5) {
//        object : Fooable {
//            override fun foo(x: String) {
//                println(x)
//            }
//        }.foo(a.x.toString())
//    }
    println(a.x)
}

object Obj {
    fun bar(x: Int): Int {
        return x * 2
    }
}

fun main() {

    try {
        foo(A(42))
    } catch (e: Throwable) {
        e.printStackTrace()
        throw e
    }
}