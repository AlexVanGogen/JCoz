package jcoz.profile.format;

import jcoz.profile.Experiment;

public interface ExperimentOutputFormat {
    byte[] format(Experiment e);
}
