#include "../helpers.cpp"

using namespace std;
using namespace eosio;

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
  rewardsv2_table_t _rewards(get_self(), get_first_receiver().value);
  auto rewards_itr = _rewards.find( devname.value );
  if ( rewards_itr != _rewards.cend()) // if row was found
  {
    _rewards.erase( rewards_itr );
  }

  // Finally erase the sensor table
  sensorsv3_table_t _sensors(get_self(), get_first_receiver().value);
  auto sensor_itr = _sensors.find( devname.value );
  if ( sensor_itr != _sensors.cend())
    _sensors.erase( sensor_itr );

}

ACTION ascensionwx::removeoldsen( uint16_t days )
{
  // Deletes any station that hasn't submitted weather data for a certain number
  //    of days.

  require_auth( get_self() );

  uint64_t now_s = current_time_point().sec_since_epoch();

  // Convert days to seconds
  uint64_t sec_delta = days*24*60*60;
  uint64_t cutoff_time_s = now_s - sec_delta;

  rewardsv2_table_t _rewards(get_self(), get_first_receiver().value);
  sensorsv3_table_t _sensors(get_self(), get_first_receiver().value);

  // First erase observations
  weather_table_t _weather(get_self(), get_first_receiver().value);
  auto wthr_itr = _weather.begin();

  bool did_erase = false;

  //for ( auto wthr_itr = _weathr.begin(); wthr_itr != _weathr.end(); wthr_itr++ ) {
  while ( wthr_itr != _weather.end()) 
  {
    did_erase = false;

    if ( wthr_itr->unix_time_s < cutoff_time_s )
    {
      name devname = wthr_itr->devname;
      auto rewards_itr = _rewards.find( devname.value );
      auto sensor_itr = _sensors.find( devname.value );

      // Also check "time_created" because very new sensors may have not been turned on yet
      if ( sensor_itr->time_created < cutoff_time_s )
      {
        if ( sensor_itr != _sensors.cend() )
          _sensors.erase( sensor_itr );
        
        if ( rewards_itr != _rewards.cend() )
          _rewards.erase( rewards_itr );
        
        if ( wthr_itr != _weather.cend() )
          wthr_itr = _weather.erase( wthr_itr );

        did_erase = true;
      }
    }
    
    if (did_erase==false)
      wthr_itr++; // Iterate to the next point
  }

}

ACTION ascensionwx::removereward( name devname )
{

  // Require auth from self
  require_auth( get_self() );

  rewardsv2_table_t _rewards(get_self(), get_first_receiver().value);
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

  rewardsv2_table_t _rewards(get_self(), get_first_receiver().value);

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
  minersv2_table_t _miners(get_self(), get_first_receiver().value);
  auto miners_itr = _miners.find( miner.value );
  if ( miners_itr != _miners.cend() )
    _miners.erase( miners_itr ); // remove the miner from Miner table

  builders_table_t _builders(get_self(), get_first_receiver().value);
  auto builders_itr = _builders.find( miner.value );
  if ( builders_itr != _builders.cend() )
    _builders.erase( builders_itr ); // remove the builder from Builder table

}
