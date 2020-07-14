package jcoz.profile.format;

import jcoz.profile.Experiment;

public class CozExperimentOutputFormat implements ExperimentOutputFormat {
    @Override
    public byte[] format(Experiment e) {
        return (e.toString() + "\n").getBytes();
    }
}
