package edu.avgogen.jcoz.tools.ppfinder;

import java.util.HashMap;
import java.util.concurrent.ConcurrentHashMap;

public class HitsRecorder {
    private static final ConcurrentHashMap<String, HashMap<Integer, Integer>> hits = new ConcurrentHashMap<>();

    static {
        Runtime.getRuntime().addShutdownHook(new Thread(() -> hits.forEach((className, lineHits) -> {
            System.out.println("Class " + className);
            lineHits.forEach((lineNumber, hits) -> System.out.println("\tline " + lineNumber + ": " + hits));
        })));
    }

    public static void registerHit(String className, int lineNumber) {
        hits.putIfAbsent(className, new HashMap<>());
        synchronized (hits.get(className)) {
            hits.get(className).compute(lineNumber, (ignored, b) -> b == null ? 1 : b + 1);
        }
    }
}
