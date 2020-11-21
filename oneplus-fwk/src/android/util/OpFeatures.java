package android.util;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.BitSet;

public final class OpFeatures {

    private static String LOG_TAG = OpFeatures.class.getSimpleName();

    private static final BitSet sFeatures = new BitSet();

    static {
        File file = new File("/odm/etc/odm_feature_list");

        try (BufferedReader br = new BufferedReader(new FileReader(file))) {
            for (String line = br.readLine(); line != null; line = br.readLine()) {
                String[] values = line.split(" ");
                if (values.length == 4 && values[3].equals("true")) {
                    String id = values[0].replaceAll("[^0-9]", "");
                    if (id.length() != 0) {
                        sFeatures.set(Integer.parseInt(id));
                    }
                }
            }
        } catch (IOException e) {
            Log.e(LOG_TAG, "Failed to read odm feature list file", e);
        }
    }

    public static boolean isSupport(int... features) {
        for (int feature : features) {
            if (feature < 0 || !sFeatures.get(feature)) {
                return false;
            }
        }
        return true;
    }
}
