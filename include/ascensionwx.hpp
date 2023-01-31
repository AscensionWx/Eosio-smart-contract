#include <eosio/eosio.hpp>
#include <evm/evm.hpp>
#include <delphioracle.hpp>

#include <math.h> // pow() and trig functions

using namespace std;
using namespace eosio;

CONTRACT ascensionwx : public contract {
  public:
    using contract::contract;

    // Should be called by device

    ACTION updatefirm( string latest_firmware_version );
    
    ACTION newperiod( uint64_t period_start_time ); // also updates tlos_usd rate

    ACTION addsensor( name devname,
                      name station_type,
                      float latitude_city,
                      float longitude_city );

    ACTION addnoaa( string station_str,
                    float latitude_city,
                    float longitude_city );

    ACTION addminer( name devname,
                     name miner,
                     bool if_picture); // also looks up if in EVM

    ACTION addevmminer( name devname, 
                        string evm_address_str,
                        bool if_picture );

    ACTION addbuilder( name devname,
                       name builder,
                       string enclosure_type);

    ACTION pushdatanoaa( string station_str,
                         uint64_t unix_time_s,
                         float temperature_c);

    ACTION pushdatafull(name devname,
                        float pressure_hpa,
                        float temperature_c, 
                        float humidity_percent,
                        float wind_dir_deg,
                        float wind_spd_ms,
                        float rain_1hr_mm,
                        uint32_t light_intensity_lux,
                        uint8_t uv_index);

    ACTION pushdata3dp(name devname,
                        float pressure_hpa,
                        float temperature_c, 
                        float humidity_percent,
                        uint32_t battery_millivolt,
                        uint8_t device_flags);

    ACTION submitgps( name devname,
                      float latitude_deg,
                      float longitude_deg,                      
                      float elev_gps_m,
                      float lat_deg_geo,
                      float lon_deg_geo);

    ACTION setpicture( name devname, bool ifpicture );

    ACTION setevmaddr( name miner_or_builder,
                       string evm_address_str );

    ACTION chngrateall( name station_type,
                        float poor_rate,
                        float good_rate,
                        float great_rate);

    ACTION chngrate1(  name devname,
                        float poor_rate,
                        float good_rate,
                        float great_rate );
    
    ACTION manualpayall( int num_hours, string memo );

    ACTION addsenstype( name station_type,
                      name token_contract );

    ACTION addtoken( name contract,
                     string symbol_letters,
                     uint8_t precision,
                     bool usd_denominated );

    ACTION addflag( uint64_t bit_value,
                    string processing_step,
                    string issue,
                    string explanation );
    
    ACTION removesensor( name devname ); // deletes from reward and weather table too
    ACTION removeoldsen( uint16_t days );
    ACTION removeminer(name miner);
    ACTION removereward(name devname);
    ACTION removeobs(name devname);

    ACTION migrewards();
    ACTION migsensors();
    ACTION migminers();
    //ACTION removetoken( name token_contract );

  private:

    // Local functions (not actions)
    void sendReward( name miner, name devname );

    void updateMinerBalance( name miner, uint8_t quality_score, float rewards_multiplier );
    void updateBuilderBalance( name builder, float rewards_multiplier );
    void updateRoyaltyBalance( name person, string type );


    void payout( string to, float balance, string memo , bool evm_enabled );
    void payoutMiner( name miner, string memo, float balance );
    void payoutMiner3dp( name miner, string memo );
    void payoutMinerLockIn( name miner, string memo );
    void payoutBuilder( name builder );

    string evmLookup( name miner );
    void handleIfSensorAlreadyHere( name devname, float lat, float lon );

    float calcDistance( float lat1, float lon1, float lat2, float lon2 ); // Calculate distance between two points
    float degToRadians( float degrees );
    float calcDewPoint( float temperature, float humidity );

    bool check_bit( uint8_t device_flags, uint8_t target_flag );
    bool if_indoor_flagged( uint8_t flags, float temperature_c );
    bool if_physical_damage( uint8_t flags );

    void handleIfNewPeriod( uint64_t now );
    void handleBuilder( name devname, bool ifFirstData );
    void handleReferral( name devname, bool ifFirstData );
    void handleMiner3dp( name devname, bool ifGreatQuality );
    void handleMinerLockIn( name devname );
    bool handleWeather(name devname,
                       float pressure_hpa,
                       float temperature_c, 
                       float humidity_percent,
                       uint8_t flags);
    bool handleSensors( name devname, bool ifGreatQuality );
    void handleClimateContracts(name devname, float latitude_deg, float longitude_deg);
    void handleWind( float wind_dir_deg, float wind_spd_ms );
    void handleRain( name devname, float rain_1hr_mm );


    void set_flags();
    
    /* This function encodes the noaa station id into a unique
        uint64 so that it can be inserted into a data table
    */
    static name noaa_to_uint64(string id) { 
      // Convert to lower case
      for_each(id.begin(), id.end(), [](char & c){c = tolower(c);});

      // replace all numbers with a new letter to preserve uniqueness
      replace( id.begin(), id.end(), '0', 'a');
      replace( id.begin(), id.end(), '1', 'b');
      replace( id.begin(), id.end(), '2', 'c');
      replace( id.begin(), id.end(), '3', 'd');
      replace( id.begin(), id.end(), '4', 'e');
      replace( id.begin(), id.end(), '5', 'f');
      replace( id.begin(), id.end(), '6', 'g');
      replace( id.begin(), id.end(), '7', 'h');
      replace( id.begin(), id.end(), '8', 'i');
      replace( id.begin(), id.end(), '9', 'j');
      replace( id.begin(), id.end(), '-', 'k');

      string new_str = "z" + id.substr(1,11);

      return name( new_str );
    }

    static checksum160 evm_string_to_checksum160( string evm_str ) {
      check( evm_str.substr(0,2) == "0x" , "EVM address must start with 0x!" );

      std::array<uint8_t, 20u> bytes = eosio_evm::toBin( evm_str.substr(2) );
      checksum160 evm_address160 = checksum160(bytes);

      return evm_address160;

    }

    static string evm_checksum160_to_string( checksum160 evm_address ) {

      std::array<uint8_t, 32u> bytes = eosio_evm::fromChecksum160( evm_address );
      string evm_str = "0x" + eosio_evm::bin2hex(bytes).substr(24);
      return evm_str;
    }

    TABLE parametersv1 {

        uint64_t period_start_time;
        uint64_t period_end_time;
        string latest_firmware_version;

        // Ony one row
        auto primary_key() const { return 0; }
    };
    typedef multi_index<name("parametersv1"), parametersv1> parameters_table_t;

    // Data Table List
    TABLE sensorsv3 {
        name devname;
        uint64_t devname_uint64;
        name station_type;
        uint64_t time_created;
        string message_type;

        string firmware_version;

        uint16_t voltage_mv;
        uint8_t num_uplinks_today;
        uint8_t num_uplinks_poor_today;
        uint8_t num_stations_nearby;
        float percent_uplinks_poor_previous;
        float sensor_pay_previous;
        float max_temp_today;
        float min_temp_today;

        bool if_indoor;
        bool permanently_damaged;
        bool in_transit;
        bool one_hotspot;
        bool active_this_period;
        bool active_last_period;
        bool has_helium_miner;
        bool allow_new_memo;

        auto  primary_key() const { return devname.value; }
    };
    typedef multi_index<name("sensorsv3"), sensorsv3> sensorsv3_table_t;

    TABLE weather {
      name devname;
      uint64_t unix_time_s;
      double latitude_deg;
      double longitude_deg;
      uint16_t elevation_gps_m;
      float pressure_hpa;
      float temperature_c;
      float humidity_percent;
      float dew_point;
      uint8_t flags;
      string loc_accuracy;

      auto primary_key() const { return devname.value; }
      uint64_t by_unixtime() const { return unix_time_s; }
      double by_latitude() const { return latitude_deg; }
      double by_longitude() const { return longitude_deg; }
      // TODO: custom index based on having / not having specific flag
    };
    //using observations_index = multi_index<"observations"_n, observations>;
    typedef multi_index<name("weather"), 
                        weather,
                        indexed_by<"unixtime"_n, const_mem_fun<weather, uint64_t, &weather::by_unixtime>>,
                        indexed_by<"latitude"_n, const_mem_fun<weather, double, &weather::by_latitude>>,
                        indexed_by<"longitude"_n, const_mem_fun<weather, double, &weather::by_longitude>>
    > weather_table_t;

    TABLE raincumulate {
      name devname;
      uint64_t unix_time_s;
      double latitude_deg;
      double longitude_deg;
      uint16_t elevation_gps_m;
      float rain_6hr;
      float rain_12hr;
      float rain_24hr;
      uint8_t flags;

      auto primary_key() const { return devname.value; }
      uint64_t by_unixtime() const { return unix_time_s; }
      double by_longitude() const { return longitude_deg; }
      // TODO: custom index based on having / not having specific flag
    };
    typedef multi_index<name("raincumulate"), 
                        raincumulate,
                        indexed_by<"unixtime"_n, const_mem_fun<raincumulate, uint64_t, &raincumulate::by_unixtime>>,
                        indexed_by<"longitude"_n, const_mem_fun<raincumulate, double, &raincumulate::by_longitude>>
    > raincumulate_table_t;

    TABLE rainraw {

      // Note, this table will use devname/station as the scope
      // Ideally should always have 96 rows present in this table for each of 96 data points
      uint64_t unix_time_s;
      float rain_15m_mm;
      uint8_t flags;

      auto primary_key() const { return unix_time_s; }
      // TODO: custom index based on having / not having specific flag
    };
    typedef multi_index<name("rainraw"), rainraw> rainraw_table_t;

    TABLE wind {
      name devname;
      uint64_t unix_time_s;
      double latitude_deg;
      double longitude_deg;
      uint16_t elevation_gps_m;
      float wind_dir_deg;
      float wind_spd_ms;
      uint8_t flags;

      auto primary_key() const { return devname.value; }
      uint64_t by_unixtime() const { return unix_time_s; }
      double by_longitude() const { return longitude_deg; }
      // TODO: custom index based on having / not having specific flag
    };
    typedef multi_index<name("wind"), 
                        wind,
                        indexed_by<"unixtime"_n, const_mem_fun<wind, uint64_t, &wind::by_unixtime>>,
                        indexed_by<"longitude"_n, const_mem_fun<wind, double, &wind::by_longitude>>
    > wind_table_t;

    TABLE solar {
      name devname;
      uint64_t unix_time_s;
      double latitude_deg;
      double longitude_deg;
      uint16_t elevation_gps_m;
      uint32_t light_intensity_lux;
      uint8_t uv_index;
      uint8_t flags;

      auto primary_key() const { return devname.value; }
      uint64_t by_unixtime() const { return unix_time_s; }
      double by_longitude() const { return longitude_deg; }
      // TODO: custom index based on having / not having specific flag
    };
    typedef multi_index<name("solar"), 
                        solar,
                        indexed_by<"unixtime"_n, const_mem_fun<solar, uint64_t, &solar::by_unixtime>>,
                        indexed_by<"longitude"_n, const_mem_fun<solar, double, &solar::by_longitude>>
    > solar_table_t;


    // TODO: Add state and city table

    TABLE tokensv2 {

        name token_contract;
        string symbol_letters;
        uint8_t precision; // e.g. 4 for Telos , 8 for Kanda

        bool usd_denominated;
        float current_usd_price;

        auto primary_key() const { return token_contract.value; }
    };
    typedef multi_index<name("tokensv2"), tokensv2> tokensv2_table_t;

    TABLE stationtypes {
        name station_type;
        name token_contract;

        float miner_poor_rate;
        float miner_good_rate;
        float miner_great_rate;

        auto primary_key() const { return station_type.value; }
    };
    typedef multi_index<name("stationtypes"), stationtypes> stationtypes_table_t;

    TABLE minersv2 {
      name miner;
      name token_contract;

      string evm_address_str;
      bool evm_send_enabled;

      float balance;
      float previous_day_pay_usd;

      //uint256_t get_address() const { return eosio_evm::checksum160ToAddress(address); };

      auto primary_key() const { return miner.value; }

      // This looks complicated, but actual conversion for web app would be quite easy.
      //  For example:
      //   EVM Address string     : 0xda4d167a05ddf76630d0026a46441028dadd40cc
      //   Checksum256 by_address : 000000000000000000000000da4d167a05ddf76630d0026a46441028dadd40cc
      checksum256 by_address() const { 
          return eosio_evm::pad160( evm_string_to_checksum160( evm_address_str )); };

    };
    typedef multi_index<name("minersv2"), 
                        minersv2,
                        indexed_by<"address"_n, const_mem_fun<minersv2, checksum256, &minersv2::by_address>>
    > minersv2_table_t;


    TABLE builders {
      name builder;
      name token_contract;
      string evm_address;
      float multiplier;
      bool evm_send_enabled;
      uint16_t number_devices;
      float balance;
      string enclosure_type;

      auto primary_key() const { return builder.value; }
    };
    typedef multi_index<name("builders"), builders> builders_table_t;

    TABLE sponsors {
      name sponsor;
      name token_contract;
      string evm_address;
      float multiplier;
      bool evm_send_enabled;
      uint16_t number_devices;
      float balance;

      auto primary_key() const { return sponsor.value; }
    };
    typedef multi_index<name("sponsors"), builders> sponsors_table_t;

    TABLE rewardsv2 {
      name devname;
      name miner;
      name builder;
      name sponsor;

      uint64_t last_miner_payout;
      uint64_t last_builder_payout;
      uint64_t last_sponsor_payout;

      float poor_quality_rate;
      float good_quality_rate;
      float great_quality_rate;

      bool picture_sent;
      bool unique_location;
      string recommend_memo;

      auto primary_key() const { return devname.value; }
      uint64_t by_miner() const { return miner.value; }
    };
    typedef multi_index<name("rewardsv2"), 
                        rewardsv2,
                        indexed_by<"miner"_n, const_mem_fun<rewardsv2, uint64_t, &rewardsv2::by_miner>>
    > rewardsv2_table_t;

    // TODO: separate flags by scope ("weather_n", "rain", etc.)
    TABLE flags {
      uint64_t bit_value;
      string processing_step;
      string issue;
      string explanation;

      auto primary_key() const { return bit_value; }
    };
    typedef multi_index<name("flags"), flags> flags_table_t;

};
