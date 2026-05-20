#include "config.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include <QDateTime>
#include <QLocale>

#include <wx/app.h>
#include <wx/event.h>
#include <wx/evtloop.h>
#include <wx/fileconf.h>
#include <wx/jsonval.h>
#include <wx/timer.h>

#include <gtest/gtest.h>

#include "model/ais_decoder.h"
#include "model/ais_defs.h"
#include "model/ais_state_vars.h"
#include "model/cli_platform.h"
#include "model/comm_ais.h"
#include "model/comm_appmsg_bus.h"
#include "model/comm_bridge.h"
#include "model/comm_drv_file.h"
#include "model/comm_drv_registry.h"
#include "model/comm_navmsg_bus.h"
#include "model/config_vars.h"
#include "model/datetime.h"
#include "model/ipc_api.h"
#include "model/logger.h"
#include "model/multiplexer.h"
#include "model/navutil_base.h"
#include "model/ocpn_types.h"
#include "model/ocpn_utils.h"
#include "model/own_ship.h"
#include "model/routeman.h"
#include "model/select.h"
#include "model/semantic_vers.h"
#include "model/std_instance_chk.h"
#include "observable_confvar.h"
#include "ocpn_plugin.h"

#include "test_config.h"

class DateTimeFormatTest : public ::testing::Test {
protected:
  void SetUp() override {
// Save original timezone environment variable
#ifdef _MSC_VER
    char* tz_env = nullptr;
    _dupenv_s(&tz_env, nullptr, "TZ");
    original_tz = tz_env ? tz_env : "";
    if (tz_env) free(tz_env);
#else
    const char* tz_env = getenv("TZ");
    original_tz = tz_env ? tz_env : "";
#endif
  }

  void TearDown() override {
    // Restore original timezone
    if (original_tz.empty()) {
#ifdef _MSC_VER
      _putenv_s("TZ", "");
#else
      unsetenv("TZ");
#endif
    } else {
#ifdef _MSC_VER
      _putenv_s("TZ", original_tz.c_str());
#else
      setenv("TZ", original_tz.c_str(), 1);
#endif
    }
    // Force timezone reload
    tzset();
  }

  // Helper method to set timezone for testing
  void SetTimezone(const std::string& tz) {
#ifdef _MSC_VER
    _putenv_s("TZ", tz.c_str());
    _tzset();  // Force reload of timezone data
#else
    setenv("TZ", tz.c_str(), 1);
    tzset();  // Force reload of timezone data
#endif
  }

  // Build a UTC QDateTime from wall-clock parts.
  static QDateTime MakeUtc(int year, int month, int day, int hour, int minute,
                           int second) {
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second),
                     Qt::UTC);
  }

  std::string original_tz;
};

TEST_F(DateTimeFormatTest, LMTTimeZoneOneHourEast) {
  QDateTime testDate = MakeUtc(2023, 1, 22, 7, 0, 0);
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%A, %B %d, %Y %I:%M:%S %p")
                                   .SetTimezone("LMT")
                                   .SetLongitude(15.0);  // 15 degrees East
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts, QLocale("en_US"));
  // The time changes by 1 hour for every 15 degrees of longitude.
  EXPECT_EQ(result.toStdString(), "Sunday, January 22, 2023 08:00:00 AM LMT")
      << "Actual result: " << result.toStdString();
}

TEST_F(DateTimeFormatTest, LMTTimeZone30MinEast) {
  QDateTime testDate = MakeUtc(2023, 1, 22, 7, 0, 0);
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%A, %B %d, %Y %I:%M:%S %p")
                                   .SetTimezone("LMT")
                                   .SetLongitude(7.5);  // 7.5 degrees East
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts, QLocale("en_US"));
  // This should shift the time 30 minutes later.
  EXPECT_EQ(result.toStdString(), "Sunday, January 22, 2023 07:30:00 AM LMT")
      << "Actual result: " << result.toStdString();
}

TEST_F(DateTimeFormatTest, LMTTimeZoneWest) {
  QDateTime testDate = MakeUtc(2023, 1, 22, 7, 0, 0);
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%A, %B %d, %Y %I:%M:%S %p")
                                   .SetTimezone("LMT")
                                   .SetLongitude(-37.5);  // 37.5 degrees West
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts, QLocale("en_US"));
  // This should shift the time 2 hours and 30 minutes earlier.
  EXPECT_EQ(result.toStdString(), "Sunday, January 22, 2023 04:30:00 AM LMT")
      << "Actual result: " << result.toStdString();
}

TEST_F(DateTimeFormatTest, CustomFormatStringEnUS) {
  QDateTime testDate = MakeUtc(2023, 1, 22, 12, 45, 57);
  DateTimeFormatOptions opts =
      DateTimeFormatOptions().SetFormatString("%A, %B %d, %Y %I:%M:%S %p");
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts, QLocale("en_US"));
  EXPECT_EQ(result.toStdString(), "Sunday, January 22, 2023 12:45:57 PM UTC")
      << "Actual date: " << result.toStdString();
}

TEST_F(DateTimeFormatTest, CustomFormatStringUTC) {
  QDateTime testDate = MakeUtc(2023, 1, 22, 12, 45, 57);
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%Y-%m-%d %H:%M:%S")
                                   .SetTimezone("UTC");
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts);
  EXPECT_EQ(result.toStdString(), "2023-01-22 12:45:57 UTC")
      << "Actual date: " << result.toStdString();
}

TEST_F(DateTimeFormatTest, CustomFormatStringEST) {
  // 12:45:57 EDT == 16:45:57 UTC
  QDateTime testDate = MakeUtc(2023, 1, 22, 16, 45, 57);
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%Y-%m-%d %H:%M:%S")
                                   .SetTimezone("UTC");
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts);
  EXPECT_EQ(result.toStdString(), "2023-01-22 16:45:57 UTC");
}

TEST_F(DateTimeFormatTest, CustomTimeZone) {
  QDateTime testDate = MakeUtc(2023, 1, 1, 12, 0, 0);
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%Y-%m-%d %H:%M:%S")
                                   .SetTimezone("EST");
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts);
  // As of now, the function doesn't handle custom timezone strings, so it
  // should default to UTC.
  EXPECT_TRUE(result.endsWith("UTC"));
}

TEST_F(DateTimeFormatTest, ShowTimezoneTrue) {
  QDateTime testDate = MakeUtc(2023, 1, 22, 12, 45, 57);
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%Y-%m-%d %H:%M:%S")
                                   .SetTimezone("UTC")
                                   .SetShowTimezone(true);
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts);
  // Should include the timezone in the output.
  EXPECT_EQ(result.toStdString(), "2023-01-22 12:45:57 UTC");
}

TEST_F(DateTimeFormatTest, ShowTimezoneFalse) {
  QDateTime testDate = MakeUtc(2023, 1, 22, 12, 45, 57);
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%Y-%m-%d %H:%M:%S")
                                   .SetTimezone("UTC")
                                   .SetShowTimezone(false);
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts);
  // Should NOT include the timezone in the output.
  EXPECT_EQ(result.toStdString(), "2023-01-22 12:45:57")
      << "Actual result: '" << result.toStdString() << "'";
}

TEST_F(DateTimeFormatTest, ShowTimezoneDefault) {
  QDateTime testDate = MakeUtc(2023, 1, 22, 12, 45, 57);
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%Y-%m-%d %H:%M:%S")
                                   .SetTimezone("UTC");
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts);
  // Default should be true.
  EXPECT_EQ(result.toStdString(), "2023-01-22 12:45:57 UTC");
}

#ifdef HAS_EN_US
// Test with Local Time in EST timezone
TEST_F(DateTimeFormatTest, LocalTimezoneEST) {
  QDateTime testDate = MakeUtc(2023, 2, 22, 12, 45, 57);

  // Set timezone to EST for this test (UTC-5)
#ifdef _MSC_VER
  SetTimezone("EST+5");
#else
  SetTimezone("EST");
#endif

  QLocale us_locale("en_US");

  // Test 1: request to format in Local Time with custom format.
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("%A, %B %d, %Y %H:%M:%S")
                                   .SetTimezone("Local Time");
  QString result = ocpn::toUsrDateTimeFormat(testDate, opts, us_locale);
  std::string s = result.toStdString();
  EXPECT_TRUE(s.find("Wednesday, February 22, 2023 07:45:57") == 0)
      << "Actual date/time: " << s;
  // Check for timezone abbreviation since we set it to EST
  EXPECT_TRUE(result.contains(" EST") || result.contains("LOC"))
      << "Actual timezone: " << s;
  // Test 2: request with default date/time format.
  opts = DateTimeFormatOptions().SetTimezone("Local Time");
  result = ocpn::toUsrDateTimeFormat(testDate, opts, us_locale);
  EXPECT_TRUE(result.contains("Wed 2/22/23 7:45:57 AM") ||
              result.contains("Wed 02/22/2023 07:45:57 AM"))
      << "Actual date/time: " << result.toStdString();

  // Test 3: request with date/time format set to $weekday_short_date_time
  opts = DateTimeFormatOptions()
             .SetFormatString("$weekday_short_date_time")
             .SetTimezone("Local Time");
  result = ocpn::toUsrDateTimeFormat(testDate, opts, us_locale);
  EXPECT_TRUE(result.contains("Wed 2/22/23 7:45:57 AM") ||
              result.contains("Wed 02/22/2023 07:45:57 AM"))
      << "Actual date/time: " << result.toStdString();

  // Test 4: request to format date/time in UTC.
  opts = DateTimeFormatOptions()
             .SetFormatString("%A, %B %d, %Y %H:%M:%S")
             .SetTimezone("UTC");
  result = ocpn::toUsrDateTimeFormat(testDate, opts, us_locale);
  EXPECT_EQ(result.toStdString(), "Wednesday, February 22, 2023 12:45:57 UTC")
      << "Actual date/time: '" << result.toStdString() << "'";

  // Test 5: request to format in Local Time with $hour_minutes_seconds
  // format.
  opts = DateTimeFormatOptions()
             .SetFormatString("$hour_minutes_seconds")
             .SetTimezone("Local Time");
  result = ocpn::toUsrDateTimeFormat(testDate, opts, us_locale);
  std::string str = result.toStdString();
  EXPECT_TRUE(str.find("7:45:57 AM EST") != std::string::npos ||
              str.find("07:45:57 AM EST") != std::string::npos ||
              str.find("07:45:57 AM LOC") != std::string::npos)
      << "Actual date/time: '" << str << "'";
}

#endif  // HAS_EN_US

#ifdef HAS_SV_SE
// Test with Local Time in CET timezone with Swedish locale (Västeuropa,
// sommartid)
TEST_F(DateTimeFormatTest, LocalTimezoneCETSwedish) {
  // Set timezone to CET (Central European Time, UTC+1) with Swedish locale
  SetTimezone("CET-1CEST");
  QLocale locale("sv_SE");

  // Summer time date (DST active)
  QDateTime testDateSummer = MakeUtc(2024, 6, 21, 15, 15, 31);

  // Test 1: ISO format with Local Time and timezone shown
  DateTimeFormatOptions opts = DateTimeFormatOptions()
                                   .SetFormatString("$weekday_short_date_time")
                                   .SetTimezone("Local Time");
  QString result = ocpn::toUsrDateTimeFormat(testDateSummer, opts, locale);
  EXPECT_TRUE(result.startsWith("Fri 2024-06-21 17:15:31"))
      << "Actual date/time: " << result.toStdString();
  EXPECT_TRUE(result.contains(" CEST") || result.contains("LOC"))
      << "Actual timezone: " << result.toStdString();
  EXPECT_FALSE(result.contains("sommartid"))
      << "Should not contain 'sommartid' suffix: " << result.toStdString();

  // Test 2: With timezone display turned off
  opts = DateTimeFormatOptions()
             .SetFormatString("%Y-%m-%d %H:%M:%S")
             .SetTimezone("Local Time")
             .SetShowTimezone(false);
  result = ocpn::toUsrDateTimeFormat(testDateSummer, opts, locale);
  EXPECT_EQ(result.toStdString(), "2024-06-21 17:15:31")
      << "Actual date/time: " << result.toStdString();

  // Test 3: US date format with Local Time
  opts = DateTimeFormatOptions()
             .SetFormatString("%a %m/%d/%Y %I:%M:%S %p")
             .SetTimezone("Local Time");
  result = ocpn::toUsrDateTimeFormat(testDateSummer, opts, locale);
  EXPECT_TRUE(result.startsWith("Fri 06/21/2024"))
      << "Actual date/time: " << result.toStdString();

  EXPECT_TRUE(result.contains("05:15:31 PM CEST") ||
              result.contains("05:15:31 PM LOC"))
      << "Actual time portion: " << result.toStdString();
  EXPECT_FALSE(result.contains("sommartid"))
      << "Should not contain 'sommartid' suffix: " << result.toStdString();
}

#endif  // SV_SE
