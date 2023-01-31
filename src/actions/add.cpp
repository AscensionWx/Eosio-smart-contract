#include "../helpers.cpp"

using namespace std;
using namespace eosio;

ACTION ascensionwx::addnoaa(string station_str,
                            float latitude_city,
                            float longitude_city ) {

  // Only own contract can add a new sensor
  require_auth( get_self() );

  name station = noaa_to_uint64( station_str );

  // Create row in the weather table
  weather_table_t _weather(get_self(), get_first_receiver().value );
  _weather.emplace(get_self(), [&](auto& wthr) {
    wthr.devname = station;
    wthr.latitude_deg = latitude_city;
    wthr.longitude_deg = longitude_city;
    wthr.loc_accuracy = "None";
  });

}

ACTION ascensionwx::addsensor( name devname,
                               name station_type,
                              float latitude_city,
                              float longitude_city ) {

  // Only own contract can add a new sensor
  require_auth( get_self() );

  uint64_t unix_time_s = current_time_point().sec_since_epoch();
  name default_token = "eosio.token"_n;

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  string message_type;
  if (station_type == name("ascension3dp"))
    message_type = "p/t/rh";
  else if (station_type == name("sensecapfull"))
    message_type = "p/t/rh/wind/rain/solar";
  else if (station_type == name("wunderground"))
    message_type = "p/t/rh/wind/rain";
  else
    check( false, "Station type not recognized." );

  // Scope is self
  sensorsv3_table_t _sensors(get_self(), get_first_receiver().value);

  // Add the row to the observation set
  _sensors.emplace(get_self(), [&](auto& sensor) {
    sensor.devname = devname;
    sensor.devname_uint64 = devname.value;
    sensor.time_created = unix_time_s;
    sensor.message_type = message_type; // Change in future
    sensor.station_type = station_type;

    sensor.firmware_version = "0.4.5";

    sensor.voltage_mv = 0;
    sensor.num_uplinks_today = 0;
    sensor.num_uplinks_poor_today = 0;
    sensor.num_stations_nearby = 0;
    sensor.percent_uplinks_poor_previous = 0;
    sensor.sensor_pay_previous = 0;
    sensor.max_temp_today = 0;
    sensor.min_temp_today = 0;

    sensor.if_indoor = false;
    sensor.permanently_damaged = false;
    sensor.in_transit = false;
    sensor.one_hotspot = false;
    sensor.active_this_period = false;
    sensor.active_last_period = false;
    sensor.has_helium_miner = false;
    sensor.allow_new_memo = true;


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

  stationtypes_table_t _types(get_self(), get_first_receiver().value);
  auto types_itr = _types.find( station_type.value );

  // Set rewards 
  rewardsv2_table_t _rewards( get_self(), get_first_receiver().value );
  _rewards.emplace( get_self(), [&](auto& reward) {

    reward.devname = devname;

    reward.last_miner_payout = 0;
    reward.last_builder_payout = 0;
    reward.last_sponsor_payout = 0;

    reward.poor_quality_rate = types_itr->miner_poor_rate;
    reward.good_quality_rate = types_itr->miner_good_rate;
    reward.great_quality_rate = types_itr->miner_great_rate;

    reward.picture_sent = false;
    reward.unique_location = true;
    reward.recommend_memo = "";
  });

  
}

ACTION ascensionwx::addminer( name devname,
                               name miner,
                               bool if_picture ) {

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
  rewardsv2_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );

  _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
      reward.miner = miner;
      reward.picture_sent = false;
      reward.recommend_memo = "";
      reward.last_miner_payout = 0;
      reward.picture_sent = if_picture;
  });

  // Add miner to miners table if not already present.
  minersv2_table_t _miners(get_self(), get_first_receiver().value);
  auto miner_itr = _miners.find( miner.value );
  if ( miner_itr == _miners.cend() ) {
    _miners.emplace(get_self(), [&](auto& minerobj) {
      minerobj.miner = miner;
      minerobj.token_contract = "eosio.token"_n;
      minerobj.evm_address_str = "0x0000000000000000000000000000000000000000";
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

ACTION ascensionwx::addevmminer( name devname, 
                                 string evm_address_str,
                                 bool if_picture ) {

  require_auth(get_self());

  minersv2_table_t _miners(get_self(), get_first_receiver().value);

  rewardsv2_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );

  bool miner_found = false;
  
  // First see if evm_address already in Miner's table. If it's already there copy that 
  //     miner's name to devname's miner in rewards table
  // NOTE: This lookup can be slow when there is large number of sensors, but of course
  //     this action will be called by hand and not very frequently

  checksum160 evm_address160 = evm_string_to_checksum160( evm_address_str );

  auto miner_address_index = _miners.get_index<"address"_n>();
  auto miner_itr = miner_address_index.find( eosio_evm::pad160(evm_address160) );
                        
  if ( miner_itr != miner_address_index.cend() ) {

    // Bring miner's name to the rewards table
    auto rewards_itr = _rewards.find( devname.value );
    _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
        reward.miner = miner_itr->miner;
        reward.picture_sent = if_picture;
    });
    miner_found = true;

  }


  /* TO DO: DELETE this section
  for ( auto miner_itr=_miners.begin(); miner_itr != _miners.end() ; miner_itr ++ ) {

      // When EVM address matches copy miner's name to rewards table
      if ( miner_itr->evm_address == evm_address_str ) {
        auto rewards_itr = _rewards.find( devname.value );



        miner_found = true;
        break; // Exit for loop
      }
  } // end for loop
  */

  // .  Finds upper bound of uint64_t conversion of in miners table evmminer5555
  // .  (should be something like evmmineraaaa)
  //  Add 16 to this "highest" name (this will make it something like evmmineraaab)
  if ( miner_found == false ) {

    // First create the EVM address, if needed
    evm_table_t _evmaccounts( name("eosio.evm"), name("eosio.evm").value );
    auto acct_index = _evmaccounts.get_index<"byaddress"_n>();
  
    checksum256 address = eosio_evm::pad160(evm_address160);
    auto evm_itr = acct_index.find( address );

    // If EVM never been created, call openwallet function to add it
    if ( evm_itr == acct_index.cend() ) {
      action(
        permission_level{ get_self(), "active"_n },
        "eosio.evm"_n , "openwallet"_n,
        std::make_tuple( get_self(), evm_address160)
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

      minerobj.token_contract = "eosio.token"_n;
      minerobj.evm_address_str = evm_address_str;
      minerobj.evm_send_enabled = true;
      minerobj.balance = 0.0;
      minerobj.previous_day_pay_usd = 0;
    });

    // Bring miner's name to the rewards table
    auto rewards_itr = _rewards.find( devname.value );
    _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
        reward.miner = new_miner_name;
        reward.recommend_memo = "";
        reward.last_miner_payout = 0;
        reward.picture_sent = if_picture;
    });

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
  rewardsv2_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );

  // Todo: If sensor has not been added yet, add the sensor

  _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
      reward.builder = builder;
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

ACTION ascensionwx::addtoken( name token_contract,
                              string symbol_letters,
                              uint8_t precision,
                              bool usd_denominated ) { // also adds token to rewards table
  // Only self can run this Action
  require_auth(get_self());

  tokensv2_table_t _tokensv2(get_self(), get_first_receiver().value);
  _tokensv2.emplace(get_self(), [&](auto& token) {
    token.token_contract = token_contract;
    token.symbol_letters = symbol_letters;
    token.precision = precision;
    token.usd_denominated = usd_denominated;
  });

}


ACTION ascensionwx::addsenstype( name station_type,
                                name token_contract ) { // also adds token to rewards table
    // Only self can run this Action
  require_auth(get_self());

  // Delete the fields currently in the rewards table
  stationtypes_table_t _types(get_self(), get_first_receiver().value);
  _types.emplace(get_self(), [&](auto& type) {
    type.station_type = station_type;
    type.token_contract = token_contract;
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
