
#ifndef HEADERFILE_H
#define HEADERFILE_H

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

void ascensionwx::updateRoyaltyBalance( name person, string type ) {

  if ( type == "builder" )
  {
    builders_table_t _builders(get_self(), get_first_receiver().value);
    auto builders_itr = _builders.find( person.value );

    float usd_amt = 0.25; // Update $0.25 per sensor
    float token_amount = usd_amt;

    _builders.modify( builders_itr, get_self(), [&](auto& builder) {
        builder.balance = builders_itr->balance + token_amount;
        builder.number_devices = builders_itr->number_devices + 1;
    });
  }
  else if ( type == "referral" )
  {
    sponsors_table_t _sponsors(get_self(), get_first_receiver().value);
    auto sponsors_itr = _sponsors.find( person.value );

    float usd_amt = 0.10; // Update $0.25 per sensor
    float token_amount = usd_amt;

    _sponsors.modify( sponsors_itr, get_self(), [&](auto& sponsor) {
        sponsor.balance = sponsors_itr->balance + token_amount;
        sponsor.number_devices = sponsors_itr->number_devices + 1;
    });
  }

}


void ascensionwx::payoutMiner3dp( name miner, string memo ) {

  tokensv2_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  minersv2_table_t _miners(get_self(), get_first_receiver().value);
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
    string memo = miners_itr->evm_address_str;

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
    miner.previous_day_pay_usd = miners_itr->balance;
    miner.balance = 0;
  });

}

void ascensionwx::payoutMiner( name miner, string memo, float balance ) {

  minersv2_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( miner.value );

  string to;
  bool evm_enabled = miners_itr->evm_send_enabled;
  if ( evm_enabled )
    to = miners_itr->evm_address_str;
  else
    to = miner.to_string();

  payout( to, balance, memo , evm_enabled );

  // Set Miner's new balance to 0
  //    Querying get_deltas with RAM payer being self will return prior daily payouts
  _miners.modify( miners_itr, get_self(), [&](auto& miner) {
    miner.previous_day_pay_usd = balance;
    miner.balance = 0;
  });

}

void ascensionwx::payoutBuilder( name builder ) {

  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( builder.value );

  // Put number of devices in payout string
  uint16_t num_devices = builders_itr->number_devices;
  string memo = "Builder payout: " + to_string(num_devices) + " devices reported data today.";

  string to;
  bool evm_enabled = builders_itr->evm_send_enabled;
  if ( evm_enabled )
    to = builders_itr->evm_address;
  else
    to = builder.to_string();

  float balance = builders_itr->balance;
  payout( to, balance, memo , evm_enabled );

  // Set Builder's new balance to 0
  _builders.modify( builders_itr, get_self(), [&](auto& builder) {
    builder.balance = 0;
    builder.number_devices = 0;
  });

}

void ascensionwx::payout( string to, float balance, string memo , bool evm_enabled )
{
  if (evm_enabled)
    check( to.substr(0,2) == "0x" , "AscensionWx payout function not called properly" );

  tokensv2_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  float usd_price = tokens_itr->current_usd_price;
  float token_amount  = balance / usd_price;

  // Check for null balance
  if ( token_amount == 0 )
    return;

  uint32_t amt_number = (uint32_t)(pow( 10, tokens_itr->precision ) * 
                                        token_amount);
  
  eosio::asset reward = eosio::asset( 
                          amt_number,
                          symbol(symbol_code( tokens_itr->symbol_letters ), tokens_itr->precision));

  if( evm_enabled )
  {
    action(
      permission_level{ get_self(), "active"_n },
      "eosio.token"_n , "transfer"_n,
      std::make_tuple( get_self(), "eosio.evm"_n, reward, to )
    ).send();

  } else {

    // Do token transfer using an inline function
    //   This works even with "iot" or another account's key being passed, because even though we're not passing
    //   the traditional "active" key, the "active" condition is still met with @eosio.code
    action(
      permission_level{ get_self(), "active"_n },
      tokens_itr->token_contract , "transfer"_n,
      std::make_tuple( get_self(), name(to), reward, memo)
    ).send();

  }
  
}

void ascensionwx::handleIfNewPeriod( uint64_t now )
{

  parameters_table_t _parameters(get_self(), get_first_receiver().value);
  auto parameters_itr = _parameters.begin();

  // Final check is to see if we need to update the period
  if ( now > parameters_itr->period_end_time ) {
    action(
        permission_level{ get_self(), "active"_n },
        get_self(), "newperiod"_n,
        std::make_tuple( parameters_itr->period_end_time ) // set start time to last period's end time
    ).send();
  }

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

  rewardsv2_table_t _rewards( get_self(), get_first_receiver().value );

  bool another_sensor_here = false;
  uint64_t now = current_time_point().sec_since_epoch();

  bool unique;

  for ( auto itr = lon_itr ; itr->longitude_deg == longitude_deg && itr != lon_index.end() ; itr++ )
  {
    if ( itr->latitude_deg == latitude_deg )
    {
      // Change all other sensors in exact same location to have multiplier = 0
      if ( itr->devname != devname ) {
        unique = false;
        another_sensor_here = true;
      } else
        unique = true;

      auto rewards_itr = _rewards.find( itr->devname.value );
      _rewards.modify(rewards_itr, get_self(), [&](auto& reward) {
        reward.unique_location = unique;
      });

    } // End same exact latitude check
  } // End same exact longitude check

  // Finally, if no sensors in same location, but multiplier
  //    was set to 0 in the past, then set multiplier to 1
  auto this_rewards_itr = _rewards.find( devname.value );
  if ( another_sensor_here == false && this_rewards_itr->unique_location == false )
      _rewards.modify(this_rewards_itr, get_self(), [&](auto& reward) {
        reward.unique_location = true;
      });

}

#endif