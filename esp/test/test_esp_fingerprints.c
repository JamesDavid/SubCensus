/* test_esp_fingerprints.c — fingerprints.csv parse + row build + k-NN classify (System §6, §7). */
#include <string.h>

#include "esp_fingerprints.h"
#include "sc_knn.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_fingerprints\n");

    /* parse a fingerprints.csv data row */
    const char* line =
        "fp00001,433920000,OOK,350,1050,,45,2857,8,5,PT2262 remote,remote,user,,,,";
    ScFingerprint fp;
    char name[32];
    SC_CHECK(esp_fingerprint_parse_line(line, &fp, name, sizeof(name)), "parsed");
    SC_CHECK_INT(fp.fv.freq_bin, 433920000);
    SC_CHECK_INT(fp.fv.modulation, SC_MOD_OOK);
    SC_CHECK_INT(fp.fv.n_sym_dur, 2);
    SC_CHECK_INT(fp.fv.sym_dur_us[0], 350);
    SC_CHECK_INT(fp.fv.sym_dur_us[1], 1050);
    SC_CHECK_INT(fp.fv.repeat_count, 5);
    SC_CHECK_STR(fp.device_name, "PT2262 remote");
    SC_CHECK_INT(fp.device_class, 8); /* 'remote' index in the taxonomy */

    /* the parsed fingerprint classifies a near-identical query via shared k-NN (System §6) */
    ScFingerprint cands[1];
    cands[0] = fp;
    ScKnnQuery q;
    memset(&q, 0, sizeof(q));
    q.fv.freq_bin = 433920000;
    q.fv.modulation = SC_MOD_OOK;
    q.fv.sym_dur_us[0] = 355;
    q.fv.sym_dur_us[1] = 1040;
    q.fv.n_sym_dur = 2;
    q.fv.n_symbols = 44;
    q.fv.est_bitrate = 2850;
    q.fv.preamble_len = 8;
    q.fv.repeat_count = 5;
    q.cadence_class = SC_CADENCE_NONE;
    ScKnnMatch m[1];
    size_t k = sc_knn_match(&q, cands, 1, m, 1);
    SC_CHECK_INT(k, 1);
    SC_CHECK(m[0].confidence > 0.8, "near match high confidence");

    /* build a row for the confirm-append active-learning loop (source=user) */
    char row[160];
    int n = esp_fingerprint_row("fp00002", &q.fv, "Porch remote", "remote", row, sizeof(row));
    SC_CHECK(n > 0, "row built");
    SC_CHECK(strstr(row, "433920000,OOK,355,1040,") != NULL, "waveform serialized");
    SC_CHECK(strstr(row, ",Porch remote,remote,user,") != NULL, "label + source=user");

    /* round-trip: the built row parses back to the same vector */
    ScFingerprint fp2;
    char name2[32];
    SC_CHECK(esp_fingerprint_parse_line(row, &fp2, name2, sizeof(name2)), "roundtrip parse");
    SC_CHECK_INT(fp2.fv.sym_dur_us[0], 355);
    SC_CHECK_INT(fp2.device_class, 8);

    return sc_test_summary();
}
