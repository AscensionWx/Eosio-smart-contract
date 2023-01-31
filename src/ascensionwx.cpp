#include <ascensionwx.hpp>

#include <eosio/asset.hpp>
#include <eosio/system.hpp>

#include "helpers.cpp"
#include "actions/submit/pushdata.cpp"
#include "actions/submit/submitgps.cpp"
#include "actions/remove.cpp"
#include "actions/set.cpp"
#include "actions/add.cpp"

#include "actions/migrate.cpp"


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

  bool isActive = false;
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
  tokensv2_table_t _tokens(get_self(), get_first_receiver().value);
  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  _tokens.modify(tokens_itr, get_self(), [&](auto& token) {
    token.current_usd_price = usd_price;
  });

  // Loop over all sensors
  sensorsv3_table_t _sensorsv3(get_self(), get_first_receiver().value);
  for ( auto itr2 = _sensorsv3.begin(); itr2 != _sensorsv3.end(); itr2++ ) {

    isActive = itr2->active_this_period;

    // Shift active_this_period to active_last_period
    _sensorsv3.modify( itr2, get_self(), [&](auto& station) {
      station.active_last_period = isActive;
      station.active_this_period = false;
    });
    
  }
  
}

ACTION ascensionwx::manualpayall( int num_hours, string memo ) {
  
  // Pay all miners max pay for x number of hours. Spawns a large number of inline actions,
  //  but eosio.token transfers are very lightweight
  //  Note: Useful for if server goes offline for a short time

  require_auth(get_self());

  minersv2_table_t _miners(get_self(), get_first_receiver().value);
  tokensv2_table_t _tokens(get_self(), get_first_receiver().value);

  auto tokens_itr = _tokens.find( "eosio.token"_n.value );

  for ( auto miners_itr=_miners.begin(); miners_itr != _miners.end() ; miners_itr ++ ) {

      name miner_n = miners_itr->miner;
      bool evm_enabled = miners_itr->evm_send_enabled;
      if( !evm_enabled && !is_account( miner_n ) )
        continue;

      // Calculate number of tokens based on 15-minute observation interval (4 per hour)
      float usd_price = tokens_itr->current_usd_price;
      float token_amount = (0.0050 * 4 * num_hours) / usd_price;

      uint32_t amt_number = (uint32_t)(pow( 10, tokens_itr->precision ) * 
                                        token_amount);

      eosio::asset reward = eosio::asset( 
                            amt_number,
                            symbol(symbol_code( tokens_itr->symbol_letters ), tokens_itr->precision));

      if( evm_enabled )
      {
        // Have to override preferred memo field for evm transfer
        string memo_evm = miners_itr->evm_address_str;

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

ACTION ascensionwx::chngrateall(  name station_type,
                                  float poor_rate,
                                  float good_rate,
                                  float great_rate) {

  /* This function can be used to change 1 or all pay quality rate */

  require_auth( get_self() );

  stationtypes_table_t _types(get_self(), get_first_receiver().value);
  auto types_itr = _types.find( station_type.value );

  _types.modify(types_itr, get_self(), [&](auto& type) {
    type.miner_poor_rate = poor_rate;
    type.miner_good_rate = good_rate;
    type.miner_great_rate = great_rate;
  });
}

ACTION ascensionwx::chngrate1( name devname,
                                  float poor_rate,
                                  float good_rate,
                                  float great_rate) {

  /* This function changes rate of specific station */

  require_auth( get_self() );

    // Set rewards 
  rewardsv2_table_t _rewards( get_self(), get_first_receiver().value );
  auto reward_itr = _rewards.find( devname.value );

  _rewards.modify(reward_itr, get_self(), [&](auto& reward) {
    reward.poor_quality_rate = poor_rate;
    reward.good_quality_rate = good_rate;
    reward.great_quality_rate = great_rate;
  });
}


// Dispatch the actions to the blockchain
EOSIO_DISPATCH(ascensionwx, (updatefirm)\
                            (newperiod)\
                            (addnoaa)\
                            (addsensor)\
                            (addminer)\
                            (addevmminer)\
                            (addbuilder)\
                            (pushdatanoaa)\
                            (pushdatafull)\
                            (pushdata3dp)\
                            (submitgps)\
                            (setpicture)\
                            (setevmaddr)\
                            (chngrateall)\
                            (chngrate1)\
                            (manualpayall)\
                            (addsenstype)
                            (addtoken)\
                            (addflag)\
                            (removesensor)\
                            (removeoldsen)\
                            (removeobs)\
                            (removeminer)\
                            (removereward)\
                            (migrewards)\
                            (migsensors)\
                            (migminers))
