#include "../../helpers.cpp"

using namespace std;
using namespace eosio;

ACTION ascensionwx::pushdatanoaa( string station_str,
                                  uint64_t unix_time_s,
                                  float temperature_c) {

  require_auth( get_self() );

  name station = noaa_to_uint64( station_str );
      
  // Get device row in weather table
  weather_table_t _weather(get_self(), get_first_receiver().value );
  auto this_weather_itr = _weather.find( station.value ); // In case the owner has multiple devices

  // Since any account can technically call this action, we first check that the device exists
  //    in the sensors table
  string error = "Station " + name{station}.to_string() + " not in table.";
  check( this_weather_itr != _weather.cend() , error);

  if ( this_weather_itr == _weather.cend() ) {
    // Create row in the weather table
    _weather.emplace(get_self(), [&](auto& wthr) {
      wthr.devname = station;
    });
  } else {
    // Update the weather table
    _weather.modify(this_weather_itr, get_self(), [&](auto& wthr) {
      wthr.unix_time_s = unix_time_s;
      wthr.temperature_c = temperature_c;
    });
  }
    
}

ACTION ascensionwx::pushdatafull(name devname,
                                float pressure_hpa,
                                float temperature_c, 
                                float humidity_percent,
                                float wind_dir_deg,
                                float wind_spd_ms,
                                float rain_1hr_mm,
                                uint32_t light_intensity_lux,
                                uint8_t uv_index) {

  uint64_t now = current_time_point().sec_since_epoch();

  // Check that permissions are met
  if( is_account( devname ) && has_auth(devname) )
      require_auth( devname ); // if device didn't sign it, then own contract should have
  else
      require_auth( get_self() );

  uint8_t flags = 0;
  bool ifGreatQuality = handleWeather( devname,
                                       pressure_hpa,
                                       temperature_c, 
                                       humidity_percent,
                                       flags );

  // Push rain data to tables                                     
  handleRain( devname, rain_1hr_mm );
  
  // Update Miner and Builder balances and send (if necessary)
  //handleMiner3dp( devname, ifGreatQuality );
  handleMinerLockIn( devname );

  bool ifFirstData = handleSensors( devname, ifGreatQuality );

  //handleBuilder( devname, ifFirstData );
  //handleReferral( devname, ifFirstData );

  // Handle input into climate contracts
  //handleClimateContracts(devname, lat1, lon1);

  handleIfNewPeriod( now );
    
}

ACTION ascensionwx::pushdatatrh(name devname,
                                float temperature_c, 
                                float humidity_percent) {

  uint64_t now = current_time_point().sec_since_epoch();

  // Check that permissions are met
  if( is_account( devname ) && has_auth(devname) )
      require_auth( devname ); // if device didn't sign it, then own contract should have
  else
      require_auth( get_self() );

  uint8_t flags = 0;
  float pressure_hpa = 0;
  bool ifGreatQuality = handleWeather( devname,
                                       pressure_hpa,
                                       temperature_c, 
                                       humidity_percent,
                                       flags );
  

  // Update Miner and Builder balances and send (if necessary)
  //handleMiner3dp( devname, ifGreatQuality );
  handleMinerLockIn( devname );

  bool ifFirstData = handleSensors( devname, ifGreatQuality );

  //handleBuilder( devname, ifFirstData );

  // Handle input into climate contracts
  //handleClimateContracts(devname, lat1, lon1);

  handleIfNewPeriod( now );
    
}

ACTION ascensionwx::pushdata3dp(name devname,
                                float pressure_hpa,
                                float temperature_c, 
                                float humidity_percent,
                                uint32_t battery_millivolt,
                                uint8_t device_flags) {

  uint64_t now = current_time_point().sec_since_epoch();

  // Check that permissions are met
  if( is_account( devname ) && has_auth(devname) )
      require_auth( devname ); // if device didn't sign it, then own contract should have
  else
      require_auth( get_self() );

  bool ifGreatQuality = handleWeather( devname,
                                       pressure_hpa,
                                       temperature_c, 
                                       humidity_percent,
                                       device_flags );
  
  // Update Miner and Builder balances and send (if necessary)
  //handleMiner3dp( devname, ifGreatQuality );
  handleMinerLockIn( devname );

  bool ifFirstData = handleSensors( devname, ifGreatQuality );

  handleBuilder( devname, ifFirstData );

  // Handle input into climate contracts
  //handleClimateContracts(devname, lat1, lon1);

  handleIfNewPeriod( now );
    
}

ACTION ascensionwx::pushdatapm(name devname,
                               uint16_t flags,
                               uint32_t pm1_0_ug_m3, 
                               uint32_t pm2_5_ug_m3, 
                               uint32_t pm4_0_ug_m3, 
                               uint32_t pm10_0_ug_m3, 
                               uint32_t pm1_0_n_cm3,
                               uint32_t pm2_5_n_cm3, 
                               uint32_t pm4_0_n_cm3, 
                               uint32_t pm10_0_n_cm3,
                               uint32_t typ_size_nm) {

  uint64_t now = current_time_point().sec_since_epoch();

  // Check that permissions are met
  if( is_account( devname ) && has_auth(devname) )
      require_auth( devname ); // if device didn't sign it, then own contract should have
  else
      require_auth( get_self() );
  
  //handleClimateContracts(devname, lat1, lon1);
    
}


void ascensionwx::handleRain( name devname, float rain_1hr_mm )
{
  uint64_t now_s = current_time_point().sec_since_epoch();
  uint64_t cutoff_1hr = now_s - 60*60;
  uint64_t cutoff_6hr = now_s - 60*60*6;
  uint64_t cutoff_24hr = now_s - 60*60*24;

  // Use devname as scope
  rainraw_table_t _rainraw( get_self(), devname.value);
  auto rainraw_itr = _rainraw.begin(); 

  // Delete all datapoints older than 24 hours
  while( rainraw_itr->unix_time_s < cutoff_24hr )
  {
    rainraw_itr = _rainraw.erase( rainraw_itr );
  }

  // Add new raw rain datapoint
  _rainraw.emplace(get_self(), [&](auto& rain) {
      rain.unix_time_s = now_s;
      rain.rain_1hr_mm = rain_1hr_mm;
      rain.flags = 0;
  });

  float sum_1hr = 0;
  float sum_6hr = 0;
  float sum_24hr = 0;
  float sum;

  uint8_t n_1hr = 0;
  float avg_1hr;

  // Store rainfall totals in cumulative rain table
  raincumulate_table_t _raincumulate( get_self(), get_first_receiver().value );
  auto raincumulate_itr = _raincumulate.find( devname.value );

  if ( raincumulate_itr == _raincumulate.cend() ) 
  {
    weather_table_t _weather(get_self(), get_first_receiver().value);
    auto weather_itr = _weather.find( devname.value );

    _raincumulate.emplace(get_self(), [&](auto& rain) {
      rain.devname = devname;
      rain.latitude_deg = weather_itr->latitude_deg;
      rain.longitude_deg = weather_itr->longitude_deg;
      rain.flags = 0;
    });

    raincumulate_itr = _raincumulate.find( devname.value );

  }

  // Calculate hourly rain
  auto itr = _rainraw.lower_bound( cutoff_1hr );
  while( itr != _rainraw.end() )
  {
    // Will get us to the most recent hourly report
    sum_1hr = itr->rain_1hr_mm;
    itr++;
  }

  // Add up 6-hourly rain
  itr = _rainraw.lower_bound( cutoff_6hr );
  while( itr != _rainraw.end() )
  {
    if ( itr->rain_1hr_mm > 0 ) 
    {
      // Add this hourly rate to sum of 6 hours
      sum_6hr = sum_6hr + itr->rain_1hr_mm;
      // Skip ahead to 1 hour later. We do this because
      //     the rain_1hr_mm is an hourly rate and we dont want to
      // .   add multiple instances
      itr = _rainraw.lower_bound( itr->unix_time_s + 60*60 );
    }
    else
      itr++;
  }

  // Add up all rain in last 24 hours
  itr = _rainraw.lower_bound( cutoff_24hr );
  while( itr != _rainraw.end() )
  {
    if ( itr->rain_1hr_mm > 0 ) 
    {
      sum_24hr = sum_24hr + itr->rain_1hr_mm;
      itr = _rainraw.lower_bound( itr->unix_time_s + 60*60 ); // Skip ahead to 1 hour later
    }
    else
      itr++;
  }

    // Finally update the hourly rain
  _raincumulate.modify( raincumulate_itr, get_self(), [&](auto& rain) {
      rain.unix_time_s = now_s;
      rain.rain_1hr = sum_1hr;
      rain.rain_6hr = sum_6hr;
      rain.rain_24hr = sum_24hr;
      rain.flags = 0;
  });

}

void ascensionwx::handleBuilder( name devname,
                                 bool ifFirstData )
{
  uint64_t unix_time_s = current_time_point().sec_since_epoch();

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  // Set rewards 
  rewardsv2_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find( devname.value );
  name builder = rewards_itr->builder;

  // If first datapoint, increase builder pay by $0.25
  if( ifFirstData )
    if ( is_account(builder) && rewards_itr->unique_location )
      updateRoyaltyBalance( builder, "builder" );

  // If Builder needs to be paid, then do Builder payout
  if ( name{builder}.to_string() != "" &&
       rewards_itr->last_builder_payout < parameters_itr->period_start_time ) {
    // Loop over the entire device reward table
    for ( auto itr = _rewards.begin(); itr != _rewards.end(); itr++ ) {
      // If builder matches current builder, then update last_miner_payout to current time.
      if ( itr->builder == builder ) {
        _rewards.modify( itr, get_self(), [&](auto& reward) {
          reward.last_builder_payout = unix_time_s;
        });
      }
    } // end loop over all rewards
    payoutBuilder( builder );
  } // end last_builder_payout check
}

void ascensionwx::handleReferral( name devname,
                                  bool ifFirstData )
{
  uint64_t unix_time_s = current_time_point().sec_since_epoch();

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  // Set rewards 
  rewardsv2_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find( devname.value );
  name referrer = rewards_itr->sponsor;

  // If first datapoint, increase builder pay by $0.25
  if( ifFirstData )
    if ( is_account(referrer) && rewards_itr->unique_location )
      updateRoyaltyBalance( referrer, "referral" );

  // If Builder needs to be paid, then do Builder payout
  if ( name{referrer}.to_string() != "" &&
       rewards_itr->last_sponsor_payout < parameters_itr->period_start_time ) {
    // Loop over the entire device reward table
    for ( auto itr = _rewards.begin(); itr != _rewards.end(); itr++ ) {
      // If builder matches current builder, then update last_miner_payout to current time.
      if ( itr->sponsor == referrer ) {
        _rewards.modify( itr, get_self(), [&](auto& reward) {
          reward.last_sponsor_payout = unix_time_s;
        });
      }
    } // end loop over all rewards
    payoutBuilder( referrer );
  } // end last_builder_payout check
}

void ascensionwx::handleMiner3dp( name devname,
                                   bool ifGreatQuality )
{

  uint64_t unix_time_s = current_time_point().sec_since_epoch();

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  // Set rewards 
  rewardsv2_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find( devname.value );
  name miner = rewards_itr->miner;

  if ( name{miner}.to_string() != "" ) {

    minersv2_table_t _miners(get_self(), get_first_receiver().value);
    auto miners_itr = _miners.find( miner.value );

    if ( rewards_itr->unique_location ) {
      //updateMinerBalance( miner, quality_score, rewards_itr->multiplier );

      // We have devname pay for RAM (should be 0), so that we can query get_deltas
      // .   with this information
      float quality_increment;
      if ( ifGreatQuality )
        quality_increment = rewards_itr->great_quality_rate;
      else
        quality_increment = rewards_itr->good_quality_rate;

      _miners.modify( miners_itr, devname, [&](auto& miner) {
        miner.balance = miners_itr->balance + quality_increment;
      });
    }
  } // end if miner check

  // If new period has begun, then do Miner payout
  if ( rewards_itr->last_miner_payout < parameters_itr->period_start_time )
  {
    // Loop over the entire device reward table
    for ( auto itr = _rewards.begin(); itr != _rewards.end(); itr++ ) {
      // If miner matches current miner, then update last_miner_payout to current time.
      if ( itr->miner == miner )
        _rewards.modify( itr, get_self(), [&](auto& reward) {
          reward.last_miner_payout = unix_time_s;
        });
    }
    payoutMiner3dp( miner, "Miner payout" );
  }

}

void ascensionwx::handleMinerLockIn( name devname )
{

  uint64_t unix_time_s = current_time_point().sec_since_epoch();

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  // Get miner value
  rewardsv2_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find( devname.value );

  name miner = rewards_itr->miner;

  // If new period has begun, then do Miner payout
  if ( rewards_itr->last_miner_payout < parameters_itr->period_start_time &&
        name{miner}.to_string() != ""  )
  {
    // First calculate how much to send
    float balance = 0;

    // Create second iterator using 2ndary index
    auto rewards_miner_index = _rewards.get_index<"miner"_n>();
    auto rewards_itr2 = rewards_miner_index.lower_bound( miner.value );

    sensorsv3_table_t _sensors(get_self(), get_first_receiver().value);

    // Loop over all stations that match the proposed miner
    while( rewards_itr2 != rewards_miner_index.cend() && rewards_itr2->miner == miner )
    {
      name devname = rewards_itr2->devname;
      auto sensors_itr = _sensors.find( devname.value );

      float station_pay = 0;
      float poor_percent = 0;
      float poor_uplinks;
      float num_uplinks = sensors_itr->num_uplinks_today;

      if ( num_uplinks > 0 )
      {
        float poor_uplinks = sensors_itr->num_uplinks_poor_today;
        poor_percent = poor_uplinks / num_uplinks;

        if ( num_uplinks > 22 && poor_percent < 0.25 )
          station_pay = rewards_itr2->great_quality_rate * 100;
        else if ( poor_percent < 0.75 )
          station_pay = rewards_itr2->good_quality_rate * 100;
        else
          station_pay = rewards_itr2->poor_quality_rate * 100;

        balance += station_pay;
      }

      _sensors.modify( sensors_itr, get_self(), [&](auto& sensor) {
        sensor.sensor_pay_previous = station_pay;
        sensor.percent_uplinks_poor_previous = poor_percent;
      });

      rewards_miner_index.modify( rewards_itr2, get_self(), [&](auto& reward) {
          reward.last_miner_payout = unix_time_s;
      });

      // Go to next station that matches miner
      rewards_itr2++;
    } // end loop over relevant miners

    // Physically payout miner
    payoutMiner( miner, "Miner payout", balance );

  }

}

bool ascensionwx::handleSensors( name devname,
                                 bool ifGreatQuality )
{
  // Find the sensor in the sensors table
  sensorsv3_table_t _sensors(get_self(), get_first_receiver().value);
  auto sensor_itr = _sensors.find( devname.value );

  bool ifFirstData;
  if (sensor_itr->active_this_period == false)
    ifFirstData = true;
  else
    ifFirstData = false;

  if (ifFirstData)
  {
    _sensors.modify(sensor_itr, get_self(), [&](auto& sensor) {
        sensor.num_uplinks_today = 0;
        sensor.num_uplinks_poor_today = 0;
        sensor.active_this_period = true;
    });
  } 

  if ( ifGreatQuality && sensor_itr->permanently_damaged == false ) {
      _sensors.modify(sensor_itr, get_self(), [&](auto& sensor) {
        sensor.num_uplinks_today = sensor_itr->num_uplinks_today + 1;
        sensor.active_this_period = true;
      });
  }
  else {
      _sensors.modify(sensor_itr, get_self(), [&](auto& sensor) {
        sensor.num_uplinks_today = sensor_itr->num_uplinks_today + 1;
        sensor.num_uplinks_poor_today = sensor_itr->num_uplinks_poor_today + 1;
        sensor.active_this_period = true;
      });
  }

  return ifFirstData;

}

// Returns if weather data is great quality
bool ascensionwx::handleWeather( name devname,
                                 float pressure_hpa,
                                 float temperature_c, 
                                 float humidity_percent,
                                 uint8_t device_flags )
{
  uint64_t unix_time_s = current_time_point().sec_since_epoch();

  // Get device row in weather table
  weather_table_t _weather(get_self(), get_first_receiver().value );
  auto this_weather_itr = _weather.find( devname.value ); // In case the owner has multiple devices

    // Since any account can technically call this action, we first check that the device exists
  //    in the sensors table
  string error = "Device " + name{devname}.to_string() + " not in table.";
  check( this_weather_itr != _weather.cend() , error);

  // Check for unphyiscal values
  uint8_t flags = device_flags;
  if (  pressure_hpa < 700 || pressure_hpa > 1100 ||
        temperature_c < -55 || temperature_c > 55 ||
        humidity_percent < 0 || humidity_percent > 100 )
    flags = device_flags + 128;

  // Update the weather table
  _weather.modify(this_weather_itr, get_self(), [&](auto& wthr) {
      wthr.unix_time_s = unix_time_s;
      wthr.pressure_hpa = pressure_hpa;
      wthr.temperature_c = temperature_c;
      wthr.humidity_percent = humidity_percent;
      wthr.dew_point = calcDewPoint( temperature_c, humidity_percent );
      wthr.flags = flags;
  });

  // If device is flagged as damaged or indoor, we know it is not good quality
  if( if_physical_damage( flags ) || if_indoor_flagged( flags, temperature_c ) )
    return false;

  // Get device's reported lat/lon
  float lat1 = this_weather_itr->latitude_deg;
  float lon1 = this_weather_itr->longitude_deg;

  /*
    We will create a lat/lon "bounding box" around the sensor in question,
      and then estimate current sensor quality based on surrounding sensors
  */

  // First determine the length of one degree of lat and one degree of lon
  float one_lat_degree_km = 111.0; // Distance between latitudes is essentially constant
  float one_lon_degree_km = (M_PI/180)*6372.8*cos(degToRadians(lat1)); // meridian distance varies by lat

  // Calculate all 4 "sides" of the box
  float nearby_sensors_km = 25.0;
  float lon_lower = lon1 - nearby_sensors_km/one_lon_degree_km;
  float lon_upper = lon1 + nearby_sensors_km/one_lon_degree_km;
  float lat_lower = lat1 - nearby_sensors_km/one_lat_degree_km;
  float lat_upper = lat1 + nearby_sensors_km/one_lat_degree_km;

  // Will return SORTED list of sensors to the "right" of the leftern-most station in the box.
  // I'm choosing longitude, because most stations are in Northern hemisphere,
  //      so stations are spread out more evenly by lon than lat
  auto lon_index = _weather.get_index<"longitude"_n>();
  auto lon_itr = lon_index.lower_bound( lon_lower );

  // Make copy of iterator in case we want to loop over sensors again in future
  //    for standard deviation or calculate median instead of mean
  auto lon_itr2 = lon_itr;

  float temperature_avg;
  float temperature_sum = 0;
  int num_nearby_good_stations=0;

  const int num_sec_30mins = 60*30;
  bool great_quality = false;

  // Remember this lon_itr list is sorted... so we can start with the left-most
  //   station in the box, and stop at the first station that falls outside the box
  for ( auto itr = lon_itr ; itr->longitude_deg < lon_upper && itr != lon_index.end() ; itr++ )
  {
    // Check if latitude constraints are met as well.
    if ( itr->latitude_deg > lat_lower && itr->latitude_deg < lat_upper )
    {
      /* At this point, the nearby sensor is inside the box */

      // If nearby sensor's last observation was over 30 minutes ago, skip it
      if ( itr->unix_time_s < unix_time_s - num_sec_30mins )
        continue;

      // If this nearby sensor is not damaged, use it in calculating average
      if( !if_physical_damage( itr->flags ) && 
          !if_indoor_flagged( itr->flags, itr->temperature_c ) ) {
          // Useful iterative algorithm for calculating average when we don't know how many 
          // .  samples beforehand
          temperature_sum = temperature_sum + itr->temperature_c;
          num_nearby_good_stations++;
      }

    }// end latitude check
  } // end longitude loop

  if ( num_nearby_good_stations > 0 )
    temperature_avg = temperature_sum / num_nearby_good_stations;
  else
    temperature_avg = temperature_c;

  // If within 2 degrees of the mean, give "great" quality score; otherwise give "good"
  if ( num_nearby_good_stations < 3 || abs(temperature_c - temperature_avg) < 5.0 )
    return true;
  else
    return false;

   /* *** More in-depth flags calculation
  //flags = calculateFlags( deviation, device_flags );

  // Update flags variable
  _weather.modify(this_weather_itr, get_self(), [&](auto& wthr) {
      wthr.flags = device_flags 
                    + ( (unix_time_s > sensor_itr->time_created + 30*3600*24) ? 0 : 1 )
                    + ( (unix_time_s > sensor_itr->last_gps_lock + 14*3600*24) ? 4 : 0 )
                    + 0
                    + 0 ; // + TODO: all other flags
  });
  */

}

void ascensionwx::handleWind( float wind_dir_deg, float wind_spd_ms  )
{
}
