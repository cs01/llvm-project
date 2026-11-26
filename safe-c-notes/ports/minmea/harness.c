/* Prints everything every parser makes of each line on stdin, so the port and
   the original can be compared. Plain C; built against either library. */
#include <stdio.h>
#include <string.h>
#include "minmea.h"

#define F(x) printf(" %ld/%ld", (long)(x).value, (long)(x).scale)
#define T(x) printf(" %d:%d:%d.%d", (x).hours, (x).minutes, (x).seconds, (x).microseconds)
#define D(x) printf(" %d-%d-%d", (x).day, (x).month, (x).year)
#define TY(x) printf(" %.5s", (x).buf)

int main(void) {
  char line[4096];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = 0;
    for (char *p = line; *p; p++)
      if (*p == '\\' && p[1] == 'r') { p[0] = '\r'; memmove(p + 1, p + 2, strlen(p + 2) + 1); }
    printf("chk %u %d %d id %d %d", minmea_checksum(line), minmea_check(line, false),
           minmea_check(line, true), minmea_sentence_id(line, false), minmea_sentence_id(line, true));
    char talker[3];
    if (minmea_talker_id(talker, line)) printf(" talker %s", talker);
    printf("\n");
    { struct minmea_sentence_rmc f; memset(&f, 0x55, sizeof f);
      if (minmea_parse_rmc(&f, line)) { printf("rmc"); TY(f.type); T(f.time); printf(" %d", f.valid); F(f.latitude); F(f.longitude); F(f.speed); F(f.course); D(f.date); F(f.variation); printf("\n"); } }
    { struct minmea_sentence_gga f; memset(&f, 0x55, sizeof f);
      if (minmea_parse_gga(&f, line)) { printf("gga"); TY(f.type); T(f.time); F(f.latitude); F(f.longitude); printf(" %d %d", f.fix_quality, f.satellites_tracked); F(f.hdop); F(f.altitude); printf(" %d", f.altitude_units); F(f.height); printf(" %d", f.height_units); F(f.dgps_age); printf("\n"); } }
    { struct minmea_sentence_gsa f; memset(&f, 0x55, sizeof f);
      if (minmea_parse_gsa(&f, line)) { printf("gsa"); TY(f.type); printf(" %d %d", f.mode, f.fix_type); for (int i = 0; i < 12; i++) printf(" %d", f.sats[i]); F(f.pdop); F(f.hdop); F(f.vdop); printf("\n"); } }
    { struct minmea_sentence_gll f; memset(&f, 0x55, sizeof f);
      if (minmea_parse_gll(&f, line)) { printf("gll"); TY(f.type); F(f.latitude); F(f.longitude); T(f.time); printf(" %d %d\n", f.status, f.mode); } }
    { struct minmea_sentence_gst f; memset(&f, 0x55, sizeof f);
      if (minmea_parse_gst(&f, line)) { printf("gst"); TY(f.type); T(f.time); F(f.rms_deviation); F(f.semi_major_deviation); F(f.semi_minor_deviation); F(f.semi_major_orientation); F(f.latitude_error_deviation); F(f.longitude_error_deviation); F(f.altitude_error_deviation); printf("\n"); } }
    { struct minmea_sentence_gsv f; memset(&f, 0x55, sizeof f);
      if (minmea_parse_gsv(&f, line)) { printf("gsv"); TY(f.type); printf(" %d %d %d", f.total_msgs, f.msg_nr, f.total_sats); for (int i = 0; i < 4; i++) { printf(" %d %d %d", f.sats[i].nr, f.sats[i].elevation, f.sats[i].azimuth); F(f.sats[i].snr); } printf("\n"); } }
    { struct minmea_sentence_vtg f; memset(&f, 0x55, sizeof f);
      if (minmea_parse_vtg(&f, line)) { printf("vtg"); TY(f.type); F(f.true_track_degrees); F(f.magnetic_track_degrees); F(f.speed_knots); F(f.speed_kph); printf(" %d\n", f.faa_mode); } }
    { struct minmea_sentence_zda f; memset(&f, 0x55, sizeof f);
      if (minmea_parse_zda(&f, line)) { printf("zda"); TY(f.type); T(f.time); D(f.date); printf(" %d %d\n", f.hour_offset, f.minute_offset); } }
    { struct minmea_sentence_gbs f; memset(&f, 0x55, sizeof f);
      if (minmea_parse_gbs(&f, line)) { printf("gbs"); TY(f.type); T(f.time); F(f.err_latitude); F(f.err_longitude); F(f.err_altitude); printf(" %d", f.svid); F(f.prob); F(f.bias); F(f.stddev); printf("\n"); } }
  }
  return 0;
}
