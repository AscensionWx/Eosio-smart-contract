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

ACTION ascensionwx::addsensor( name devname,
                              float latitude_city,
                              float longitude_city ) {

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

  string loc_accuracy;
  if ( latitude_city != 0 || longitude_city != 0 )
    loc_accuracy = "Low (Shipment city name)";
  else
    loc_accuracy = "None";

  // Create row in the weather table
  weather_table_t _weather(get_self(), get_first_receiver().value );
  _weather.emplace(get_self(), [&](auto& wthr) {
    wthr.devname = devname;
    wthr.latitude_deg = latitude_city;
    wthr.longitude_deg = longitude_city;
    wthr.loc_accuracy = loc_accuracy;
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

ACTION ascensionwx::addevmminer( name devname, 
                                 checksum160 evm_address ) {

  require_auth(get_self());

  miners_table_t _miners(get_self(), get_first_receiver().value);

  rewards_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );

  // Convert checksum160 to string by
  //   converting to hex and chopping off padded bytes
  std::array<uint8_t, 32u> bytes = eosio_evm::fromChecksum160( evm_address );
  string evm_address_str = "0x" + eosio_evm::bin2hex(bytes).substr(24);

  bool miner_found = false;
  
  // First see if evm_address already in Miner's table. If it's already there copy that 
  //     miner's name to devname's miner in rewards table
  // NOTE: This lookup can be slow when there is large number of sensors, but of course
  //     this action will be called by hand and not very frequently
  
  for ( auto miner_itr=_miners.begin(); miner_itr != _miners.end() ; miner_itr ++ ) {

      // When EVM address matches copy miner's name to rewards table
      if ( miner_itr->evm_address == evm_address_str ) {
        auto rewards_itr = _rewards.find( devname.value );

        // Bring miner's name to the rewards table
        _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
          reward.miner = miner_itr->miner;
        });

        miner_found = true;
        break; // Exit for loop
      }
  } // end for loop
  

  // .  Finds upper bound of uint64_t conversion of in miners table evmminer5555
  // .  (should be something like evmmineraaaa)
  //  Add 16 to this "highest" name (this will make it something like evmmineraaab)
  if ( miner_found == false ) {

    // First create the EVM address, if needed
    evm_table_t _evmaccounts( name("eosio.evm"), name("eosio.evm").value );
    auto acct_index = _evmaccounts.get_index<"byaddress"_n>();
  
    checksum256 address = eosio_evm::pad160(evm_address);
    auto evm_itr = acct_index.find( address );

    // If EVM never been created, call openwallet function to add it
    if ( evm_itr == acct_index.cend() ) {
      action(
        permission_level{ get_self(), "active"_n },
        "eosio.evm"_n , "openwallet"_n,
        std::make_tuple( get_self(), evm_address)
      ).send();
    }

    // Theory is that this returns the highest name "below" evmminer5555
    name miner_name = "evmminer5555"_n;
    auto miner_itr = _miners.lower_bound( miner_name.value );
    while ( miner_name.value < "evmminerzzzz"_n.value ) {
      miner_itr++;
      miner_name = miner_itr->miner;
    }

    miner_itr--;
    name last_miner_name = miner_itr->miner;
    name new_miner_name = name( last_miner_name.value + 16 );

    _miners.emplace(get_self(), [&](auto& minerobj) {
      // Set devname's miner to be slightly different from the last
      minerobj.miner = new_miner_name;
      //minerobj.miner = name( "evmmineraaac"_n.value + 16 );
      minerobj.token_contract = "eosio.token"_n;
      minerobj.evm_address = evm_address_str;
      minerobj.multiplier = 1.0;
      minerobj.evm_send_enabled = true;
      minerobj.balance = 0.0;
    });

    // Bring miner's name to the rewards table
    auto rewards_itr = _rewards.find( devname.value );
    _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
        reward.miner = new_miner_name;
    });

  }

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
  //if ( miner != devname )
  //  check( is_account(miner) , "Account doesn't exist.");

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
      minerobj.evm_address = "";
      minerobj.multiplier = 1.0;
      minerobj.evm_send_enabled = false;
      minerobj.balance = 0.0;
    });
  }

  if ( miner != devname && is_account( miner ) ) {

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
  //check( is_account(builder) , "Account doesn't exist.");

  // Add the miner to the sensors table
  rewards_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );

  // Todo: If sensor has not been added yet, add the sensor

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
      builderobj.evm_address = "";
      builderobj.multiplier = 1.0;
      builderobj.evm_send_enabled = false;
      builderobj.number_devices = 0;
      builderobj.balance = 0.0;
      builderobj.enclosure_type = enclosure_type;
    });
  }

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  // To confirm the miner can receive tokens, we send a small amount of Telos to the account
  /*
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
  */
                               
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

  // Check for phyiscal damage
  uint8_t flags = device_flags;
  if (  pressure_hpa < 700 || pressure_hpa > 1100 ||
        temperature_c < -55 || temperature_c > 55 ||
        humidity_percent < 0 || humidity_percent > 100 )
    flags = device_flags + 128;


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
      wthr.dew_point = calcDewPoint( temperature_c, humidity_percent );
  });

  // Get device's reported lat/lon
  float lat1 = this_weather_itr->latitude_deg;
  float lon1 = this_weather_itr->longitude_deg;

  // If indoors or flagged as damaged, give very low quality score
  uint8_t quality_score;
  if ( if_physical_damage( flags ) || if_indoor_flagged( flags, temperature_c ) || 
        sensor_itr->permanently_damaged || !rewards_itr->picture_sent )
    {
      quality_score=1;
    }
  // otherwise, we determine quality based on average temperatures around the sensor
  else {

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
          if( !if_physical_damage( itr->flags ) && 
              !if_indoor_flagged( itr->flags, itr->temperature_c ) ) {
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

  _weather.modify(this_weather_itr, get_self(), [&](auto& wthr) {
    wthr.flags = flags;
  });

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

  // Get period start time
  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  name miner = rewards_itr->miner;
  name builder = rewards_itr->builder;
  
  // Goes into the Miner table and updates the balances accordingly

  if ( name{miner}.to_string() != "" ) {
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
      if ( rewards_itr->recommend_memo != "" )
        payoutMiner( miner, rewards_itr->recommend_memo);
      else 
        payoutMiner( miner, "Miner payout" );

    }
  } // end if miner check

  // Handle Builder updates
  if ( name{builder}.to_string() != "" )
  {
    // If new period has begun, then do Builder payout
    if ( rewards_itr->last_builder_payout < parameters_itr->period_start_time ) {

      // Loop over the entire device reward table
      for ( auto itr = _rewards.begin(); itr != _rewards.end(); itr++ ) {
        // If builder matches current builder, then update last_miner_payout to current time.
        if ( itr->builder == builder ) {
          _rewards.modify( itr, get_self(), [&](auto& reward) {
            reward.last_builder_payout = unix_time_s;
          });
        }
      } // end loop over all rewards

      // Use Inline action to pay out builder
      payoutBuilder( builder );

    } // end last_builder_payout check
  } // end builder account check

  // Do this first time after period
  if ( sensor_itr->active_this_period == false ) {

    // Increase builder balance by $0.25
    if ( is_account(builder) )
      updateBuilderBalance( builder, rewards_itr->multiplier );

    // Set this sensor to active
    _sensors.modify(sensor_itr, get_self(), [&](auto& sensor) {
      sensor.active_this_period = true;
    });
  }

  // Handle input into climate contracts
  handleClimateContracts(devname, lat1, lon1);

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

  rewards_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find(devname.value);

  // If all fields are blank, exit the function
  if ( latitude_deg == 0 && longitude_deg == 0 && lat_deg_geo == 0 && lon_deg_geo == 0 ) return;

  // First check if this was the first position message after ship

  if ( weather_itr->loc_accuracy.substr(0,3) == "Low" )
  {
      float distance = calcDistance( lat_deg_geo, 
                                    lon_deg_geo, 
                                    weather_itr->latitude_deg, 
                                    weather_itr->longitude_deg );
      if ( distance < 30.0 ) {
        _sensors.modify(sensor_itr, get_self(), [&](auto& sensor) {
          sensor.in_transit = false;
        });
        _weather.modify(weather_itr, get_self(), [&](auto& wthr) {
          wthr.latitude_deg = lat_deg_geo;
          wthr.longitude_deg = lon_deg_geo;
          wthr.loc_accuracy = "Medium (Geolocation only)";
        });
      }
  }

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

  // If other sensors are in exact same location, reduce their multiplier to 
  if ( latitude_deg == 0 && longitude_deg == 0 )
    handleIfSensorAlreadyHere( devname, lat_deg_geo, lon_deg_geo );
  else
    handleIfSensorAlreadyHere( devname, latitude_deg, longitude_deg );

  // Update sensor in table
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

ACTION ascensionwx::setmultiply( name devname_or_miner, 
                                float multiplier ) {

  require_auth(get_self());

  rewards_table_t _rewards( get_self(), get_first_receiver().value );
  auto rewards_itr = _rewards.find( devname_or_miner.value );
  if( rewards_itr != _rewards.cend() ) {
    _rewards.modify( rewards_itr, get_self(), [&](auto& reward) {
        reward.multiplier = multiplier;
    });
    return;
  }

  miners_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( devname_or_miner.value );
  if( miners_itr != _miners.cend() )
    _miners.modify( miners_itr, get_self(), [&](auto& miner) {
        miner.multiplier = multiplier;
    });

}

ACTION ascensionwx::setbuildmult( name builder,
                                  float multiplier ) {

  require_auth(get_self());

  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( builder.value );
  if( builders_itr != _builders.cend() )
    _builders.modify( builders_itr, get_self(), [&](auto& buildr) {
        buildr.multiplier = multiplier;
    });

}


ACTION ascensionwx::setevmaddr( name miner_or_builder, 
                                checksum160 evm_address ) {

  // Only self can run this action
  require_auth( get_self() );

  evm_table_t _evmaccounts( name("eosio.evm"), name("eosio.evm").value );
  auto acct_index = _evmaccounts.get_index<"byaddress"_n>();
  
  //uint256_t address = eosio_evm::checksum160ToAddress(evm_address);
  checksum256 address = eosio_evm::pad160(evm_address);
  auto evm_itr = acct_index.find( address );

  // If EVM address is not present in the table, call openwallet function to add it
  if ( evm_itr == acct_index.cend() ) {
    action(
      permission_level{ get_self(), "active"_n },
      "eosio.evm"_n , "openwallet"_n,
      std::make_tuple( get_self(), evm_address)
    ).send();
  }

  // Convert checksum160 to string by
  //   converting to hex and chopping off padded bytes
  std::array<uint8_t, 32u> bytes = eosio_evm::fromChecksum160( evm_address );
  string evm_address_str = "0x" + eosio_evm::bin2hex(bytes).substr(24);

  // Set the miner's account to hold the EVM address string
  miners_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( miner_or_builder.value );

  if ( miners_itr != _miners.cend() )
    _miners.modify( miners_itr, get_self(), [&](auto& miner) {
      miner.evm_address = evm_address_str;
      miner.evm_send_enabled = true;
    });

  // Do same for builder's account
  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( miner_or_builder.value );

  if ( builders_itr != _builders.cend() )
    _builders.modify( builders_itr, get_self(), [&](auto& builder) {
      builder.evm_address = evm_address_str;
      builder.evm_send_enabled = true;
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

ACTION ascensionwx::manualpayall( int num_hours, string memo ) {
  
  // Pay all miners max pay for x number of hours. Spawns a large number of inline actions,
  //  but eosio.token transfers are very lightweight
  //  Note: Useful for if server goes offline for a short time

  require_auth(get_self());

  miners_table_t _miners(get_self(), get_first_receiver().value);
  tokens_table_t _tokens(get_self(), get_first_receiver().value);

  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  for ( auto miners_itr=_miners.begin(); miners_itr != _miners.end() ; miners_itr ++ ) {

      // Calculate number of tokens based on 15-minute observation interval (4 per hour)
      float token_amount = tokens_itr->miner_amt_per_great_obs * 4 * num_hours * miners_itr->multiplier;

      uint32_t amt_number = (uint32_t)(pow( 10, tokens_itr->precision ) * 
                                        token_amount);

      eosio::asset reward = eosio::asset( 
                            amt_number,
                            symbol(symbol_code( tokens_itr->symbol_letters ), tokens_itr->precision));

      if( miners_itr->evm_send_enabled )
      {
        // Have to override preferred memo field for evm transfer
        string memo_evm = miners_itr->evm_address;

        action(
          permission_level{ get_self(), "active"_n },
          "eosio.token"_n , "transfer"_n,
          std::make_tuple( get_self(), "eosio.evm"_n, reward, memo_evm )
        ).send();

      } else {
        action(
          permission_level{ get_self(), "active"_n },
          tokens_itr->token_contract , "transfer"_n,
          std::make_tuple( get_self(), miners_itr->miner, reward, memo)
        ).send();
      }

  } // end for loop
  

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

ACTION ascensionwx::rewardfactor(float factor)
{
  // Mutliply all miner reward multipliers by consistent factor
  // (Used if want to keep old rewards similar, while decreasing new reward amounts)

  require_auth( get_self() );

  miners_table_t _miners(get_self(), get_first_receiver().value);

  for ( auto miner_itr=_miners.begin(); miner_itr != _miners.end() ; miner_itr ++ ) {
      _miners.modify( miner_itr, get_self(), [&](auto& miner) {
          miner.multiplier = miner_itr->multiplier * factor;
      });
  } // end for loop
  
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

ACTION ascensionwx::removeminer(name miner)
{

  // Note: Removes either a Miner or Builder from its table
  
  // Require auth from self
  require_auth( get_self() );

  rewards_table_t _rewards(get_self(), get_first_receiver().value);

  // First, find all sensors with this miner name and set miner to empty
  for ( auto itr = _rewards.begin(); itr != _rewards.end(); itr++ ) {

    // If Miner or Builder are set in table, set to empty string
    if ( itr->miner == miner )
      _rewards.modify( itr, get_self(), [&](auto& reward) {
        reward.miner = name("");
      });
    if ( itr->builder == miner )
      _rewards.modify( itr, get_self(), [&](auto& reward) {
        reward.builder = name("");
      });
  }

  // Remove miner/builder from table
  miners_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( miner.value );
  if ( miners_itr != _miners.cend() )
    _miners.erase( miners_itr ); // remove the miner from Miner table

  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( miner.value );
  if ( builders_itr != _builders.cend() )
    _builders.erase( builders_itr ); // remove the builder from Builder table

}


void ascensionwx::updateMinerBalance( name miner, uint8_t quality_score, float rewards_multiplier ) {

  // If messages are broadcast every 15 minutes, this function will be called about
  //   100 times per day

  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  miners_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( miner.value );

  float usd_amt;

  if ( quality_score == 3 )
    usd_amt = tokens_itr->miner_amt_per_great_obs;
  else if ( quality_score == 2 )
    usd_amt = tokens_itr->miner_amt_per_good_obs;
  else if ( quality_score == 1 )
    usd_amt = tokens_itr->miner_amt_per_poor_obs;
  else
    usd_amt = 0;

  // Account for multipliers
  usd_amt = usd_amt * miners_itr->multiplier * rewards_multiplier;

  _miners.modify( miners_itr, get_self(), [&](auto& miner) {
    miner.balance = miners_itr->balance + usd_amt;
  });
  
}

void ascensionwx::updateBuilderBalance( name builder, float rewards_multiplier) {

  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( builder.value );

  float usd_amt = 0.25; // Update $0.25 per sensor

  if ( rewards_multiplier != 0 ) {
    float token_amount = usd_amt * builders_itr->multiplier;

    _builders.modify( builders_itr, get_self(), [&](auto& builder) {
      builder.balance = builders_itr->balance + token_amount;
      builder.number_devices = builders_itr->number_devices + 1;
    });
  }

}


void ascensionwx::payoutMiner( name miner, string memo ) {

  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  miners_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( miner.value );

  float usd_price = tokens_itr->current_usd_price;
  float token_amount  = miners_itr->balance / usd_price;

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

    // Override the memo
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

void ascensionwx::payoutBuilder( name builder ) {

  tokens_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( builder.value );

  float usd_price = tokens_itr->current_usd_price;

  float token_amount  = builders_itr->balance / usd_price;
  uint16_t num_devices = builders_itr->number_devices;

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

    // Put number of devices in payout string
    string memo = "Builder payout: " + to_string(num_devices) + " devices reported data today.";

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
    builder.number_devices = 0;
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

float ascensionwx::calcDewPoint( float temperature_c, float humidity ) {
  
  const float c1 = 243.04;
  const float c2 = 17.625;
  float h = humidity / 100;
  if (h <= 0.01)
    h = 0.01;
  else if (h > 1.0)
    h = 1.0;

  const float lnh = log(h); // natural logarithm
  const float tpc1 = temperature_c + c1;
  const float txc2 = temperature_c * c2;
  
  float txc2_tpc1 = txc2 / tpc1;

  float tdew = c1 * (lnh + txc2_tpc1) / (c2 - lnh - txc2_tpc1);

  return tdew;
}

bool ascensionwx::check_bit( uint8_t flags, uint8_t target_flag ) {

  for( int i=128; i>0; i=i/2 ) {

    bool flag_enabled = ( flags - i >= 0 );
    if( i==target_flag ) 
      return flag_enabled;
    if( flag_enabled )
      flags -= i;

  }

  return false; // If target flag never reached return false
}

bool ascensionwx::if_physical_damage( uint8_t flags ) {
  return check_bit(flags,128); // Flag representing phyiscal damage
}

bool ascensionwx::if_indoor_flagged( uint8_t flags, float temperature_c ) {
  
  // If temperature is between 66 and 78 farenheight, check sensor variance flag
  if ( temperature_c > 18.9 && temperature_c < 25.5 ) 
    return check_bit(flags,16); // Flag for very low temperature variance
  else
    return false;
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

void ascensionwx::handleClimateContracts(name devname, float latitude, float longitude)
{

}

void ascensionwx::handleIfSensorAlreadyHere( name devname,
                                            float latitude_deg, 
                                            float longitude_deg )
{
  // Set reward for this sensor to 0 if there is one in same exact location
  weather_table_t _weather(get_self(), get_first_receiver().value );
  auto lon_index = _weather.get_index<"longitude"_n>();
  auto lon_itr = lon_index.lower_bound( longitude_deg );

  rewards_table_t _rewards( get_self(), get_first_receiver().value );

  bool another_sensor_here = false;
  uint64_t now = current_time_point().sec_since_epoch();

  for ( auto itr = lon_itr ; itr->longitude_deg == longitude_deg && itr != lon_index.end() ; itr++ )
  {
    if ( itr->latitude_deg == latitude_deg )
    {
      // Change all other sensors in exact same location to have multiplier = 0
      float mult;
      if ( itr->devname != devname ) {
        mult = 0;
        another_sensor_here = true;
      } else
        mult = 1;

      auto rewards_itr = _rewards.find( itr->devname.value );
      _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
        reward.multiplier = mult;
      });

    } // End same exact latitude check
  } // End same exact longitude check

  // Finally, if no sensors in same location, but multiplier
  //    was set to 0 in the past, then set multiplier to 1
  auto this_rewards_itr = _rewards.find( devname.value );
  if ( another_sensor_here == false && this_rewards_itr->multiplier == 0 )
      _rewards.modify(this_rewards_itr, get_self(), [&](auto& reward) {
        reward.multiplier = 1.0;
      });

}


// Dispatch the actions to the blockchain
EOSIO_DISPATCH(ascensionwx, (updatefirm)\
                            (newperiod)\
                            (addsensor)\
                            (addminer)\
                            (addevmminer)\
                            (addbuilder)\
                            (submitdata)\
                            (submitgps)\
                            (setpicture)\
                            (setmultiply)\
                            (setbuildmult)\
                            (setevmaddr)\
                            (chngminerpay)\
                            (manualpayall)\
                            (addtoken)\
                            (addflag)\
                            (rewardfactor)\
                            (removesensor)\
                            (removeobs)\
                            (removeminer)\
                            (removereward))
