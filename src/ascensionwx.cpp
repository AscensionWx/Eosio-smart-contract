#include <ascensionwx.hpp>

#include <eosio/asset.hpp>
#include <eosio/system.hpp>

#include <delphioracle.hpp>
#include <evm/evm.hpp>

using namespace std;
using namespace eosio;

ACTION ascensionwx::updatefirm( string latest_firmware_version ) {

  require_auth( get_self() );

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  _parameters.modify(parameters_itr, get_self(), [&](auto& param) {
    param.latest_firmware_version = latest_firmware_version;
  });

}

ACTION ascensionwx::newperiod( uint64_t period_start_time ) {

  require_auth( get_self() );

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  const uint32_t seconds_in_24_hrs = 60*60*24;

  if ( parameters_itr == _parameters.cend() )
    _parameters.emplace(get_self(), [&](auto& param) {
      param.period_start_time = period_start_time;
      param.period_end_time = period_start_time + seconds_in_24_hrs;
    });
  else
    _parameters.modify(parameters_itr, get_self(), [&](auto& param) {
      param.period_start_time = period_start_time;
      param.period_end_time = period_start_time + seconds_in_24_hrs;
    });

  datapointstable _delphi_prices( name("delphioracle"), "tlosusd"_n.value );
  auto delphi_itr = _delphi_prices.begin(); // gets the latest price
  float usd_price = delphi_itr->value / 10000.0;

  // Also update rewards table to have correct tlos_usd rate
  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  _tokens.modify(tokens_itr, get_self(), [&](auto& token) {
    token.current_usd_price = usd_price;
  });

  // Loop over all sensors
  sensors_table_t _sensors(get_self(), get_first_receiver().value);
  for ( auto itr = _sensors.begin(); itr != _sensors.end(); itr++ ) {

    // Shift active_this_period to active_last_period
    bool isActive = itr->active_this_period;
    _sensors.modify( itr, get_self(), [&](auto& sensor) {
      sensor.active_last_period = isActive;
      sensor.active_this_period = false;
    });
  }

}

ACTION ascensionwx::addsensor( name devname ) {

  // Only own contract can add a new sensor
  require_auth( get_self() );

  uint64_t unix_time_s = current_time_point().sec_since_epoch();
  name default_token = "eosio.token"_n;

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  // Scope is self
  sensors_table_t _sensors(get_self(), get_first_receiver().value);

  // Add the row to the observation set
  _sensors.emplace(get_self(), [&](auto& sensor) {
    sensor.devname = devname;
    sensor.devname_uint64 = devname.value;
    sensor.time_created = unix_time_s;
    sensor.message_type = "p/t/rh"; // Change in future
    sensor.firmware_version = parameters_itr->latest_firmware_version;

    sensor.last_gps_lock = 0;
    //sensor.next_sunrise_time = calc_next_sunrise();
    //sensor.next_sunset_time = calc_next_sunset();
    sensor.day_anomaly_avg = 0;
    sensor.day_anomaly_num_samples = 0;
    sensor.night_anomaly_avg = 0;
    sensor.night_anomaly_num_samples = 0;

    sensor.low_voltage = 0;
    sensor.high_voltage = 0;
    sensor.permanently_damaged = 0;
    sensor.in_transit = 1;
    sensor.one_hotspot = 0;
    sensor.active_this_period = false;
    sensor.active_last_period = false;
    sensor.has_helium_miner = 0;
    sensor.allow_new_memo = 1;
  });

  // Create row in the weather table
  weather_table_t _weather(get_self(), get_first_receiver().value );
  _weather.emplace(get_self(), [&](auto& wthr) {
    wthr.devname = devname;
    wthr.loc_accuracy = "None ";
  });

  // Set rewards 
  rewards_table_t _rewards( get_self(), get_first_receiver().value );
  _rewards.emplace( get_self(), [&](auto& reward) {
    reward.devname = devname;

    reward.last_miner_payout = 0;
    reward.last_builder_payout = 0;
    reward.last_sponsor_payout = 0;

    reward.picture_sent = false;
    reward.recommend_memo = "";
    reward.multiplier = 1;
  });

}

ACTION ascensionwx::addminer( name devname,
                               name miner ) {

  // To allow this action to be called using the "iot"_n permission, 
  //   make sure eosio linkauth action assigns iot permission to this action
  //
  // The benefit is that the "active" key (which traditionally transfers token balances)
  //   doesn't need to be on the same server as the one with inbound internet traffic

  require_auth(get_self());

  // First check that device and miner accounts exist
  check( is_account(miner) , "Account doesn't exist.");

  // Add the miner to the sensors table
  rewards_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );

  _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
      reward.miner = miner;
      reward.picture_sent = false;
      reward.recommend_memo = "";
      reward.multiplier = 1;
      reward.last_miner_payout = current_time_point().sec_since_epoch();
  });

  // Add miner to miners table if not already present.
  miners_table_t _miners(get_self(), get_first_receiver().value);
  auto miner_itr = _miners.find( miner.value );
  if ( miner_itr == _miners.cend() ) {
    _miners.emplace(get_self(), [&](auto& minerobj) {
      minerobj.miner = miner;
      minerobj.token_contract = "eosio.token"_n;
      minerobj.evm_address = evmLookup( miner );
      minerobj.multiplier = 1.0;
      minerobj.evm_send_enabled = false;
      minerobj.balance = 0.0;
    });
  }

  // To confirm the miner can receive tokens, we send a small amount of Telos to the account
  uint8_t precision = 4;
  string memo = "Miner account enabled!";
  uint32_t amt_number = (uint32_t)(pow( 10, precision ) * 
                                        0.0001);
  eosio::asset reward = eosio::asset( 
                          amt_number,
                          symbol(symbol_code( "TLOS" ), precision));
  
  action(
      permission_level{ get_self(), "active"_n },
      "eosio.token"_n , "transfer"_n,
      std::make_tuple( get_self(), miner, reward, memo )
  ).send();
                               
}

ACTION ascensionwx::addbuilder( name devname,
                                name builder,
                                string enclosure_type ) {

  // To allow this action to be called using the "iot"_n permission, 
  //   make sure eosio linkauth action assigns iot permission to this action
  //
  // The benefit is that the "active" key (which traditionally transfers token balances)
  //   doesn't need to be on the same server as the one with inbound internet traffic

  require_auth(get_self());

  // First check that device and miner accounts exist
  check( is_account(builder) , "Account doesn't exist.");

  // Add the miner to the sensors table
  rewards_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );

  _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
      reward.builder = builder;
      reward.multiplier = 1;
      reward.last_builder_payout = current_time_point().sec_since_epoch();
  });

  // Add builder to builder table is not already present
  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builder_itr = _builders.find( builder.value );
  if ( builder_itr == _builders.cend() ) {
    _builders.emplace(get_self(), [&](auto& builderobj) {
      builderobj.builder = builder;
      builderobj.token_contract = "eosio.token"_n;
      builderobj.evm_address = evmLookup( builder );
      builderobj.multiplier = 1.0;
      builderobj.evm_send_enabled = false;
      builderobj.balance = 0.0;
      builderobj.enclosure_type = enclosure_type;
    });
  }

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  // To confirm the miner can receive tokens, we send a small amount of Telos to the account
  uint8_t precision = 4;
  string memo = "Builder account enabled!";
  uint32_t amt_number = (uint32_t)(pow( 10, precision ) * 
                                        0.0001);
  eosio::asset reward = eosio::asset( 
                          amt_number,
                          symbol(symbol_code( "TLOS" ), precision));
  
  action(
      permission_level{ get_self(), "active"_n },
      "eosio.token"_n , "transfer"_n,
      std::make_tuple( get_self(), builder, reward, memo )
  ).send();
                               
}

ACTION ascensionwx::submitdata(name devname,
                                float pressure_hpa,
                                float temperature_c, 
                                float humidity_percent,
                                uint8_t device_flags) {

  uint64_t unix_time_s = current_time_point().sec_since_epoch();

  // Check that permissions are met
  if( is_account( devname ) ) {
    if( !has_auth(devname) ) { // check if device signed transaction
      require_auth( get_self() ); // if device didn't sign it, then own contract should have
      //signature_flag = true; // Flag the data as signed by the server
    }
  } else
    require_auth( get_self() );

  // Find the sensor in the sensors table
  sensors_table_t _sensors(get_self(), get_first_receiver().value);
  auto sensor_itr = _sensors.find( devname.value );

  // Since any account can technically call this action, we first check that the device exists
  //    in the sensors table
  string error = "Device " + name{devname}.to_string() + " not in table.";
  check( sensor_itr != _sensors.cend() , error);

  // Set rewards 
  rewards_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find( devname.value );

  // Get device row in weather table
  weather_table_t _weather(get_self(), get_first_receiver().value );
  auto this_weather_itr = _weather.find( devname.value ); // In case the owner has multiple devices

  // Update the weather table
  _weather.modify(this_weather_itr, get_self(), [&](auto& wthr) {
      wthr.unix_time_s = unix_time_s;
      wthr.pressure_hpa = pressure_hpa;
      wthr.temperature_c = temperature_c;
      wthr.humidity_percent = humidity_percent;
  });

  // If indoors or flagged as damaged, give very low quality score
  uint8_t quality_score;
  if ( if_physical_damage( device_flags ) || if_indoor_flagged( device_flags ) ||
        sensor_itr->permanently_damaged || !rewards_itr->picture_sent )
    {
      quality_score=1;
    }
  // otherwise, we determine quality based on average temperatures around the sensor
  else {
  
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

      const int num_sec_30mins = 60*30;
      float avg_temperature = temperature_c;
      int num_nearby_good_stations=0;

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
          if( !if_physical_damage( itr->flags ) && !if_indoor_flagged( itr->flags ) ) {
            // Useful iterative algorithm for calculating average when we don't know how many 
            // .  samples beforehand
            avg_temperature = (avg_temperature*num_nearby_good_stations + itr->temperature_c) / 
                                          (num_nearby_good_stations+1);
            num_nearby_good_stations++;
          }

        }// end latitude check
      } // end longitude loop

      // If within 2 degrees of the mean, give "great" quality score; otherwise give "good"
      if ( num_nearby_good_stations > 1 && abs(temperature_c - avg_temperature) > 2.0 )
        quality_score = 2;
      else
        quality_score = 3;

  }  // end check for damaged/indoor flags

  //flags = calculateFlags( deviation, device_flags );

  // Update flags variable
  /*
  _weather.modify(this_weather_itr, get_self(), [&](auto& wthr) {
      wthr.flags = device_flags 
                    + ( (unix_time_s > sensor_itr->time_created + 30*3600*24) ? 0 : 1 )
                    + ( (unix_time_s > sensor_itr->last_gps_lock + 14*3600*24) ? 4 : 0 )
                    + 0
                    + 0 ; // + TODO: all other flags
  });
  */

  // Get period start time
  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  name miner = rewards_itr->miner;
  name builder = rewards_itr->builder;
  
  // Goes into the Miner table and updates the balances accordingly

  if ( is_account(miner) ) {
    updateMinerBalance( miner, quality_score, rewards_itr->multiplier );

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

      // Uses Inline action to pay out miner
      payoutMiner( miner, rewards_itr->recommend_memo);
    }
  } // end if miner check

  // Handle Builder updates
  if ( is_account(builder) )
  {
    // Goes into the Builder table and updates the balances accordingly
    if ( sensor_itr->active_this_period == false )
      updateBuilderBalance( builder );

    // If new period has begun, then do Builder payout
    if ( rewards_itr->last_builder_payout < parameters_itr->period_start_time ) {

      // Simple counter so we know how many of this builder's devices transmitted today.
      int dev_counter = 0;

      // Loop over the entire device reward table
      for ( auto itr = _rewards.begin(); itr != _rewards.end(); itr++ ) {
        // If builder matches current builder, then update last_miner_payout to current time.
        if ( itr->builder == builder ) {
          dev_counter++;
          _rewards.modify( itr, get_self(), [&](auto& reward) {
            reward.last_builder_payout = unix_time_s;
          });
        }
      } // end loop over all rewards

      // Use Inline action to pay out builder
      string memo = "Builder payout: " + to_string(dev_counter) + " devices reported data today.";
      payoutBuilder( builder,  memo);
    } // end last_builder_payout check
  } // end builder account check

  // Update sensor activity flag if set to false
  if ( sensor_itr->active_this_period == false )
    _sensors.modify(sensor_itr, get_self(), [&](auto& sensor) {
      sensor.active_this_period = true;
    });

  // Final check is to see if we need to update the period
  if ( unix_time_s > parameters_itr->period_end_time ) {
    action(
        permission_level{ get_self(), "active"_n },
        get_self(), "newperiod"_n,
        std::make_tuple( parameters_itr->period_end_time ) // set start time to last period's end time
    ).send();
  }
    
}

ACTION ascensionwx::submitgps( name devname,
                                float latitude_deg,
                                float longitude_deg,
                                float elev_gps_m,
                                float lat_deg_geo,
                                float lon_deg_geo) {

  // To allow this action to be called using the "iot"_n permission, 
  //   make sure eosio linkauth action assigns iot permission to submitgps action
  //
  // The benefit is that the "active" key (which traditionally transfers token balances)
  //   doesn't need to be on the same server as the one with inbound internet traffic

  // Require auth from self
  require_auth( get_self() );

  uint64_t unix_time_s = current_time_point().sec_since_epoch();

  // Get sensors table
  sensors_table_t _sensors(get_self(), get_first_receiver().value);
  auto sensor_itr = _sensors.find( devname.value );

  //auto dev_string = name{device};
  string error = "Device " + name{devname}.to_string() + " not in table.";
  check( sensor_itr != _sensors.cend() , error);

  weather_table_t _weather(get_self(), get_first_receiver().value);
  auto weather_itr = _weather.find( devname.value );

  // If all fields are blank, exit the function
  if ( latitude_deg == 0 && longitude_deg == 0 && lat_deg_geo == 0 && lon_deg_geo == 0 ) return;

  if ( latitude_deg != 0 && longitude_deg != 0 && lat_deg_geo == 0 && lon_deg_geo == 0 )
  {
    // If we submit lat/lon without elevation data, we can assume it's based on "shipment city"
    if (  elev_gps_m == 0 )
    {
      _weather.modify(weather_itr, get_self(), [&](auto& wthr) {
        wthr.loc_accuracy = "Low (Shipment city name)";
        wthr.latitude_deg = latitude_deg;
        wthr.longitude_deg = longitude_deg;
      });
    } else {
    // We have gps elevation, so data came from GPS
      _weather.modify(weather_itr, get_self(), [&](auto& wthr) {
        wthr.loc_accuracy = "Medium (GPS only)";
        wthr.latitude_deg = latitude_deg;
        wthr.longitude_deg = longitude_deg;
        wthr.elevation_gps_m = elev_gps_m;
      });
    }
  }

  // If only geolocation lat/lons are supplied
  if ( latitude_deg == 0 && latitude_deg == 0 && lat_deg_geo != 0 && lon_deg_geo != 0 )
  {
    _weather.modify(weather_itr, get_self(), [&](auto& wthr) {
      wthr.loc_accuracy = "Medium (Geolocation only)";
      wthr.latitude_deg = lat_deg_geo;
      wthr.longitude_deg = lon_deg_geo;
    });
  }

  // Finally, if all 4 fields are filled out, we give high confidence
  if ( latitude_deg != 0 && latitude_deg != 0 && lat_deg_geo != 0 && lon_deg_geo != 0 )
  {
      float distance = calcDistance( latitude_deg, 
                                    longitude_deg, 
                                    lat_deg_geo, 
                                    lon_deg_geo );

      // LoRaWAN gateways can hear a maximum of about 15 kilometers away
      check( distance < 15.0 ,
          "Geolocation check fail. Geolocation and GPS "+
          to_string(uint32_t(distance))+
          " km apart.");
          

      _weather.modify(weather_itr, get_self(), [&](auto& wthr) {
        wthr.loc_accuracy = "High (Geolocation + GPS)";
        wthr.latitude_deg = latitude_deg;
        wthr.longitude_deg = longitude_deg;
        wthr.elevation_gps_m = elev_gps_m;
      });

  }

  _sensors.modify(sensor_itr, get_self(), [&](auto& sensor) {
      sensor.last_gps_lock = unix_time_s;
      //sensor.next_sunrise_time = calc_next_sunrise();
      //sensor.next_sunset_time = calc_next_sunset();
  });

}

ACTION ascensionwx::setpicture( name devname, 
                                bool ifpicture ) {

  require_auth(get_self());

  rewards_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find( devname.value );

  _rewards.modify( rewards_itr, get_self(), [&](auto& reward) {
        reward.picture_sent = ifpicture;
  });

}

ACTION ascensionwx::setmultiply( name devname_or_acct, 
                                float multiplier ) {

  require_auth(get_self());

  rewards_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find( devname_or_acct.value );
  if( rewards_itr != _rewards.cend() ) {
    _rewards.modify( rewards_itr, get_self(), [&](auto& reward) {
        reward.multiplier = multiplier;
    });
    return;
  }

  miners_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( devname_or_acct.value );
  if( miners_itr != _miners.cend() )
    _miners.modify( miners_itr, get_self(), [&](auto& miner) {
        miner.multiplier = multiplier;
    });

  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( devname_or_acct.value );
  if( builders_itr != _builders.cend() )
    _builders.modify( builders_itr, get_self(), [&](auto& builder) {
        builder.multiplier = multiplier;
    });

}

ACTION ascensionwx::chngminerpay( name token_contract,
                                  float miner_amt_per_great_obs,
                                  float miner_amt_per_good_obs,
                                  float miner_amt_per_poor_obs ) {

  // Only self can run this Action
  require_auth(get_self());

  // Delete the fields currently in the rewards table
  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( token_contract.value );
  
  _tokens.modify( tokens_itr, get_self(), [&](auto& token) {
    token.miner_amt_per_great_obs = miner_amt_per_great_obs;
    token.miner_amt_per_good_obs = miner_amt_per_good_obs;
    token.miner_amt_per_poor_obs = miner_amt_per_poor_obs;
  });

}

ACTION ascensionwx::addtoken( name token_contract,
                              string symbol_letters,
                              bool usd_denominated,
                              uint8_t precision) { // also adds token to rewards table
    // Only self can run this Action
  require_auth(get_self());

  // Delete the fields currently in the rewards table
  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  _tokens.emplace(get_self(), [&](auto& token) {
    token.token_contract = token_contract;
    token.symbol_letters = symbol_letters;
    token.usd_denominated = usd_denominated;
    token.precision = precision;
  });
}


ACTION ascensionwx::addflag( uint64_t bit_value,
                            string processing_step,
                            string issue,
                            string explanation ) {
  
  flags_table_t _flags(get_self(), get_first_receiver().value);
  auto flag_itr = _flags.find( bit_value );

  // If flag doesn't exist, add it
  if (flag_itr==_flags.cend()) {
    _flags.emplace( get_self(), [&](auto& flag) {
      flag.bit_value = bit_value;
      flag.processing_step = processing_step;
      flag.issue = issue;
      flag.explanation = explanation;
    });
  } else { // else modify it
    _flags.modify(flag_itr, get_self(), [&](auto& flag) {
      flag.bit_value = bit_value;
      flag.processing_step = processing_step;
      flag.issue = issue;
      flag.explanation = explanation;
    });
  }

}

ACTION ascensionwx::removesensor(name devname)
{
  // Require auth from self
  require_auth( get_self() );

  // First erase observations
  weather_table_t _weather(get_self(), get_first_receiver().value);
  auto wthr_itr = _weather.find( devname.value );
  if ( wthr_itr != _weather.cend()) // if row was found
  {
      _weather.erase( wthr_itr ); // remove the row
  }

  // Erase rewards table
  rewards_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );
  if ( rewards_itr != _rewards.cend()) // if row was found
  {
    _rewards.erase( rewards_itr );
  }

  // Finally erase the sensor table
  sensors_table_t _sensors(get_self(), get_first_receiver().value);
  auto sensor_itr = _sensors.find( devname.value );
  _sensors.erase( sensor_itr );

}



ACTION ascensionwx::removereward( name devname )
{

  // Require auth from self
  require_auth( get_self() );

  rewards_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );

  _rewards.erase( rewards_itr );
}


ACTION ascensionwx::removeobs(name devname)
{
  // Require auth from self
  require_auth( get_self() );

  weather_table_t _weather(get_self(), get_first_receiver().value);
  auto itr = _weather.find( devname.value );

  _weather.erase( itr );
}


void ascensionwx::updateMinerBalance( name miner, uint8_t quality_score, float rewards_multiplier ) {

  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  miners_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( miner.value );

  float usd_price = tokens_itr->current_usd_price;
  float usd_amt;

  if ( quality_score == 3 )
    usd_amt = tokens_itr->miner_amt_per_great_obs;
  else if ( quality_score == 2 )
    usd_amt = tokens_itr->miner_amt_per_good_obs;
  else if ( quality_score == 1 )
    usd_amt = tokens_itr->miner_amt_per_poor_obs;
  else
    usd_amt = 0;

  // If messages are broadcast every 15 minutes, this comes to $0.25 to $1.00 per day
  float token_amount = ( usd_amt * miners_itr->multiplier * rewards_multiplier) / usd_price;

  _miners.modify( miners_itr, get_self(), [&](auto& miner) {
    miner.balance = miners_itr->balance + token_amount;
  });
  
}

void ascensionwx::updateBuilderBalance( name builder ) {

  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( builder.value );

  float usd_price = tokens_itr->current_usd_price;
  float usd_amt = 0.25; // Update $0.25 per sensor
  float token_amount = ( usd_amt * builders_itr->multiplier ) / usd_price;

  _builders.modify( builders_itr, get_self(), [&](auto& builder) {
    builder.balance = builders_itr->balance + token_amount;
  });

}


void ascensionwx::payoutMiner( name miner, string memo ) {

  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  miners_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( miner.value );

  float token_amount  = miners_itr->balance;

  // Check for null balance
  if ( token_amount == 0 )
    return;

  uint32_t amt_number = (uint32_t)(pow( 10, tokens_itr->precision ) * 
                                        token_amount);
    
  eosio::asset reward = eosio::asset( 
                          amt_number,
                          symbol(symbol_code( tokens_itr->symbol_letters ), tokens_itr->precision));

  if( miners_itr->evm_send_enabled )
  {

    // Convert to hex and chop off padded bytes
    string memo = miners_itr->evm_address;

    action(
      permission_level{ get_self(), "active"_n },
      "eosio.token"_n , "transfer"_n,
      std::make_tuple( get_self(), "eosio.evm"_n, reward, memo )
    ).send();

  } else {

    // Do token transfer using an inline function
    //   This works even with "iot" or another account's key being passed, because even though we're not passing
    //   the traditional "active" key, the "active" condition is still met with @eosio.code
    action(
      permission_level{ get_self(), "active"_n },
      tokens_itr->token_contract , "transfer"_n,
      std::make_tuple( get_self(), miner, reward, memo)
    ).send();

  }

  // Set Miner's new balance to 0
  _miners.modify( miners_itr, get_self(), [&](auto& miner) {
    miner.balance = 0;
  });

}

void ascensionwx::payoutBuilder( name builder, string memo ) {

  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( builder.value );

  float token_amount  = builders_itr->balance;

  // Check for null balance
  if ( token_amount == 0 )
    return;

  uint32_t amt_number = (uint32_t)(pow( 10, tokens_itr->precision ) * 
                                        token_amount);
    
  eosio::asset reward = eosio::asset( 
                          amt_number,
                          symbol(symbol_code( tokens_itr->symbol_letters ), tokens_itr->precision));

  if( builders_itr->evm_send_enabled )
  {

    // Convert to hex and chop off padded bytes
    string memo = builders_itr->evm_address;

    action(
      permission_level{ get_self(), "active"_n },
      "eosio.token"_n , "transfer"_n,
      std::make_tuple( get_self(), "eosio.evm"_n, reward, memo )
    ).send();

  } else {

    // Do token transfer using an inline function
    //   This works even with "iot" or another account's key being passed, because even though we're not passing
    //   the traditional "active" key, the "active" condition is still met with @eosio.code
    action(
      permission_level{ get_self(), "active"_n },
      tokens_itr->token_contract , "transfer"_n,
      std::make_tuple( get_self(), builder, reward, memo)
    ).send();

  }

  // Set Builder's new balance to 0
  _builders.modify( builders_itr, get_self(), [&](auto& builder) {
    builder.balance = 0;
  });


}


float ascensionwx::calcDistance( float lat1, float lon1, float lat2, float lon2 )
{
  // This function uses the given lat/lon of the devices to deterimine
  //    the distance between them.
  // The calculation is made using the Haversine method and utilizes Math.h

  float R = 6372800; // Earth radius in meters

  float phi1 = degToRadians(lat1);
  float phi2 = degToRadians(lat2);
  float dphi = degToRadians( lat2 - lat1 );
  float dlambda = degToRadians( lon2 - lon1 );

  float a = pow(sin(dphi/2), 2) + pow( cos(phi1)*cos(phi2)*sin(dlambda/2.0) , 2);

  float distance_meters = 2*R*atan2( sqrt(a) , sqrt(1-a) );

  // return distance in km
  return distance_meters/1000.0;

}

float ascensionwx::degToRadians( float degrees )
{
  // M_PI comes from math.h
    return (degrees * M_PI) / 180.0;
}

bool ascensionwx::check_bit( uint8_t device_flags, uint8_t target_flag ) {

  for( int i=128; i>0; i=i/2 ) {

    bool flag_enabled = ( device_flags - i >= 0 );
    if( i==target_flag ) 
      return flag_enabled;
    if( flag_enabled )
      device_flags -= i;

  }

  return false; // If target flag never reached return false
}

bool ascensionwx::if_physical_damage( uint8_t device_flags ) {
  return check_bit(device_flags,128); // Flag representing phyiscal damage
}

bool ascensionwx::if_indoor_flagged( uint8_t device_flags ) {
  return check_bit(device_flags,16); // Flag for very low temperature variance
}

string ascensionwx::evmLookup( name account )
{
    // Look up the EVM address from the evm contract
    evm_table_t _evmaccounts( name("eosio.evm"), name("eosio.evm").value );
    auto acct_index = _evmaccounts.get_index<"byaccount"_n>();
    auto evm_itr = acct_index.find( account.value ); // Use miner's name to query

    checksum160 evmaddress = evm_itr->address;
    std::array<uint8_t, 32u> bytes = eosio_evm::fromChecksum160( evmaddress );

    // Convert to hex and chop off padded bytes
    return "0x" + eosio_evm::bin2hex(bytes).substr(24);
}

// Dispatch the actions to the blockchain
EOSIO_DISPATCH(ascensionwx, (updatefirm)\
                            (newperiod)\
                            (addsensor)\
                            (addminer)\
                            (addbuilder)\
                            (submitdata)\
                            (submitgps)\
                            (setpicture)\
                            (setmultiply)\
                            (chngminerpay)\
                            (addtoken)\
                            (addflag)\
                            (removesensor)\
                            (removeobs)\
                            (removereward))
