package jcoz.profile.format;

import jcoz.profile.Experiment;

public class CsvExperimentOutputFormat implements ExperimentOutputFormat {
    private static int experimentCounter = 1;

    @Override
    public byte[] format(Experiment e) {
        //noinspection StringBufferReplaceableByString
        return new StringBuilder()
            .append(experimentCounter++).append(",")
            .append(e.getClassSig()).append(",")
            .append(e.getLineNo()).append(",")
            .append(e.getSpeedup()).append(",")
            .append(e.getPointsHit()).append(",")
            .append(e.getDuration()).append("\n")
            .toString()
            .getBytes();
    }
}
