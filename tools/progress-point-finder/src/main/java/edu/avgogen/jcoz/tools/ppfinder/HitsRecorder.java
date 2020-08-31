package edu.avgogen.jcoz.tools.ppfinder;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

public class HitsRecorder {
    private static final Map<Thread, Map<String, Map<Integer, SourceLineHitsStatistics>>> hits = new HashMap<>();
    private static final AtomicLong timer = new AtomicLong(0);

    static {
        Runtime.getRuntime().addShutdownHook(new Thread(() -> hits.forEach(((thread, threadHits) -> {
            System.out.println("Thread " + thread.getName());
            threadHits.forEach((className, lineHits) -> {
                System.out.println("\tClass " + className);
                lineHits.forEach((lineNumber, hits) -> System.out.println("\t\tline " + lineNumber + ": " + hits.toString()));
            });
        }))));
    }

    public static void registerHit(String className, int lineNumber) {
        long time = timer.incrementAndGet();
        hits.putIfAbsent(Thread.currentThread(), new HashMap<>());
        hits.get(Thread.currentThread()).putIfAbsent(className, new HashMap<>());
        Map<Integer, SourceLineHitsStatistics> classStatistics = hits.get(Thread.currentThread()).get(className);
        classStatistics.putIfAbsent(lineNumber, new SourceLineHitsStatistics());
        classStatistics.get(lineNumber).registerHit(time);
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
