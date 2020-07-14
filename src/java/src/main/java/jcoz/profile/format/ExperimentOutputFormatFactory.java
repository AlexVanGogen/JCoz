package jcoz.profile.format;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ExperimentOutputFormatFactory {

    private static final Logger logger = LoggerFactory.getLogger(ExperimentOutputFormatFactory.class);
    private static ExperimentOutputFormatFactory instance = null;

    private ExperimentOutputFormatFactory() {
    }

    public static ExperimentOutputFormatFactory getInstance() {
        if (instance == null) {
            instance = new ExperimentOutputFormatFactory();
        }
        return instance;
    }

    public ExperimentOutputFormat fromString(String format) {
        if ("coz".equals(format)) {
            return new CozExperimentOutputFormat();
        } else {
            logger.warn("Unknown output format: {}. Will use 'coz' as default...", format);
            return new CozExperimentOutputFormat();
        }
    }
}
