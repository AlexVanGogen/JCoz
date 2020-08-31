package edu.avgogen.jcoz.tools.ppfinder;

import java.util.HashMap;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

public class HitsRecorder {
    private static final ConcurrentHashMap<String, HashMap<Integer, SourceLineHitsStatistics>> hits = new ConcurrentHashMap<>();
    private static final AtomicLong timer = new AtomicLong(0);

    static {
        Runtime.getRuntime().addShutdownHook(new Thread(() -> hits.forEach((className, lineHits) -> {
            System.out.println("Class " + className);
            lineHits.forEach((lineNumber, hits) -> System.out.println("\tline " + lineNumber + ": " + hits.toString()));
        })));
    }

    public static void registerHit(String className, int lineNumber) {
        long time = timer.incrementAndGet();
        hits.putIfAbsent(className, new HashMap<>());
        HashMap<Integer, SourceLineHitsStatistics> classStatistics = hits.get(className);
        synchronized (hits) {
            classStatistics.putIfAbsent(lineNumber, new SourceLineHitsStatistics());
            classStatistics.get(lineNumber).registerHit(time);
        }
    }

    private static class SourceLineHitsStatistics {
        private int hits;
        private long lastHitTime;
        private long maxInterval;

        public void registerHit(long time) {
            hits++;
            maxInterval = Math.max(maxInterval, time - lastHitTime);
            lastHitTime = time;
        }

        @Override
        public String toString() {
            return hits + " hits, " + maxInterval + " max interval";
        }
    }
}
