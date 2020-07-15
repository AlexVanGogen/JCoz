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
        switch (format) {
            case "coz":
                return new CozExperimentOutputFormat();
            case "csv":
                return new CsvExperimentOutputFormat();
            default:
                logger.warn("Unknown output format: {}. Will use 'coz' as default...", format);
                return new CozExperimentOutputFormat();
        }
    }
}
