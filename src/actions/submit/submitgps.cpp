#include "../../helpers.cpp"

using namespace std;
using namespace eosio;

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
  sensorsv3_table_t _sensors(get_self(), get_first_receiver().value);
  auto sensor_itr = _sensors.find( devname.value );

  //auto dev_string = name{device};
  string error = "Device " + name{devname}.to_string() + " not in table.";
  check( sensor_itr != _sensors.cend() , error);

  weather_table_t _weather(get_self(), get_first_receiver().value);
  auto weather_itr = _weather.find( devname.value );

  rewardsv2_table_t _rewards( get_self(), get_first_receiver().value );
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

      float distance_true_geo = calcDistance( latitude_deg, 
                                              longitude_deg, 
                                              lat_deg_geo, 
                                              lon_deg_geo );

      // LoRaWAN gateways can hear a maximum of about 15 kilometers away
      /*
      check( distance < 15.0 ,
          "Geolocation check fail. Geolocation and GPS "+
          to_string(uint32_t(distance))+
          " km apart.");
      */
          
      _weather.modify(weather_itr, get_self(), [&](auto& wthr) {
        wthr.loc_accuracy = "High (Geolocation + GPS)";
        wthr.latitude_deg = latitude_deg;
        wthr.longitude_deg = longitude_deg;
        wthr.elevation_gps_m = elev_gps_m;
      });

      // If another sensor has same exact location
      if ( distance_true_geo < 0.2 ) 
        handleIfSensorAlreadyHere( devname, latitude_deg, longitude_deg );

  }

}