// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simulationcraft.hpp"

#include "util/rapidjson/document.h"
#include "util/rapidjson/stringbuffer.h"
#include "util/rapidjson/prettywriter.h"

namespace {
// source_str ===============================================================

std::string source_str( wowhead::wowhead_e source )
{
  switch ( source )
  {
    case wowhead::PTR:  return "ptr";
#if SC_BETA
    case wowhead::BETA: return SC_BETA_STR;
#endif
    default:   return "www";
  }
}

std::string source_desc_str( wowhead::wowhead_e source )
{
  switch ( source )
  {
    case wowhead::PTR:  return "PTR";
#if SC_BETA
    case wowhead::BETA: return "Beta";
#endif
    default:   return "Live";
  }
}

// This is a list of all wowhead tooltips that currently freeze the GUI when the mouse hovers over them.
// If you find any others, just add them to the array. Generally it will be a spell that has a perk linked in it.
// The underlying issue is with the QT Webkit, and we most likely cannot fix it. There's a possibility that QT 5.4 
// will correct this issue, but we won't know for another month or 2. - 9/14/14
static bool dual_tooltip_disable( unsigned /*spell_id*/)
{
/*
  static const unsigned naughtylist[] = { 1719, 53817, 20608, 26573, 51690, 57755,
    157738, 50401, 118038, 115939, 116849, 88766, 115151, 84608, 115295, 115151,
    53563, 1178, 50334, 106951, 108558, 603, 32645, 145416, 32645, 53301, 145661,
    61777, 49206, 158392, 24275, 158392, 348, 102342, 59052, 51271, 774, 101546,
    22482, 49222, 51753, 9484, 81209, 81206, 81208, 116858, 172, 63560, 49576,
    770, 53365, 116, 6940, 48181, 80240, 157708, 6544, 1680, 772, 12328, 86346,
    29144, 71, 123829, 31935, 156910, 84608, 85739, 324, 8050, 73680, 165462, 5171,
    84654, 47541, 53351, 49020, 130736, 82692, 24858, 78674, 1822, 103103, 135288,
    5487, 30451, 12051, 12042, 133, 108853, 44614, 12472, 31687, 20473, 53385,
    1856, 79140, 11327, 157584, 2818, 19434, 51713, 17, 45242, 15407, 5308, 163558,
    163201, 121411, 79136, 129197, 106952 };
  for ( size_t i = 0; i < sizeof_array( naughtylist ); i++ )
    if ( spell_id == naughtylist[ i ] )
      return true;
*/
  return false;
}

// download_id ==============================================================

std::shared_ptr<xml_node_t> download_id( sim_t*             sim,
                                unsigned           id,
                                cache::behavior_e  caching,
                                wowhead::wowhead_e source )
{
  if ( ! id )
    return std::shared_ptr<xml_node_t>();

  std::string url_www = "http://" + source_str( source ) + ".wowhead.com/item="
                        + util::to_string( id ) + "&xml";

  std::shared_ptr<xml_node_t> node = xml_node_t::get( sim, url_www, caching, "</json>" );
  if ( sim -> debug && node ) node -> print();
  return node;
}

} // unnamed namespace

// wowhead::download_glyph ==================================================

bool wowhead::download_glyph( player_t*          player,
                              std::string&       glyph_name,
                              const std::string& glyph_id,
                              wowhead_e          source,
                              cache::behavior_e  caching )
{
  unsigned glyphid = strtoul( glyph_id.c_str(), 0, 10 );
  std::shared_ptr<xml_node_t> node = download_id( player -> sim, glyphid, caching, source );
  if ( ! node || ! node -> get_value( glyph_name, "name/cdata" ) )
  {
    if ( caching != cache::ONLY )
      player -> sim -> errorf( "Unable to download glyph id %s from wowhead\n", glyph_id.c_str() );
    return false;
  }

  return true;
}

// download_item_data =======================================================

bool wowhead::download_item_data( item_t&            item,
                                  cache::behavior_e  caching,
                                  wowhead_e          source )
{
  std::shared_ptr<xml_node_t> xml = item.xml = download_id( item.sim, item.parsed.data.id, caching, source );

  if ( ! xml )
  {
    if ( caching != cache::ONLY )
      item.sim -> errorf( "Player %s unable to download item id '%u' from wowhead at slot %s.\n", item.player -> name(), item.parsed.data.id, item.slot_name() );
    return false;
  }

  try
  {
    int id;
    if ( ! xml -> get_value( id, "item/id" ) ) throw( "id" );
    item.parsed.data.id = id;

    if ( ! xml -> get_value( item.name_str, "name/cdata" ) ) throw( "name" );
    util::tokenize( item.name_str );

    xml -> get_value( item.icon_str, "icon/cdata" );

    if ( ! xml -> get_value( item.parsed.data.level, "level/." ) ) throw( "level" );

    if ( ! xml -> get_value( item.parsed.data.quality, "quality/id" ) ) throw( "quality" );

    std::string jsonequipdata, jsondata;
    xml -> get_value( jsonequipdata, "jsonEquip/cdata" );
    jsonequipdata = "{" + jsonequipdata + "}";
    xml -> get_value( jsondata, "json/cdata" );
    jsondata = "{" + jsondata + "}";

    rapidjson::Document json, jsonequip;
    json.Parse< 0 >( jsondata.c_str() );
    jsonequip.Parse< 0 >( jsonequipdata.c_str() );

    if ( json.HasParseError() )
    {
      item.sim -> errorf( "Unable to parse JSON data for item id '%u': %s", 
          id, json.GetParseError() );
      return false;
    }

    if ( jsonequip.HasParseError() )
    {
      item.sim -> errorf( "Unable to parse JSON data for item id '%u': %s", 
          id, jsonequip.GetParseError() );
      return false;
    }

    if ( item.sim -> debug )
    {
      rapidjson::StringBuffer b;
      rapidjson::PrettyWriter< rapidjson::StringBuffer > writer( b );

      json.Accept( writer );
      item.sim -> out_debug.raw() << b.GetString();

      jsonequip.Accept( writer );
      item.sim -> out_debug.raw() << b.GetString();
    }

    if ( ! json.HasMember( "slot" ) )
      throw( "inventory type" );

    if ( ! json.HasMember( "classs" ) )
      throw( "item class" );

    if ( ! json.HasMember( "subclass" ) )
      throw( "item subclass" );

    item.parsed.data.inventory_type = json[ "slot" ].GetInt();
    item.parsed.data.item_class = json[ "classs" ].GetInt();
    item.parsed.data.item_subclass = json[ "subclass" ].GetInt();
    if ( item.parsed.data.item_subclass < 0 )
      item.parsed.data.item_subclass = 0;

    if ( json.HasMember( "reqlevel" ) )
      item.parsed.data.req_level = json[ "reqlevel" ].GetInt();

    if ( json.HasMember( "raidfinder" ) )
      item.parsed.data.type_flags |= RAID_TYPE_LFR;

    if ( json.HasMember( "heroic" ) )
      item.parsed.data.type_flags |= RAID_TYPE_HEROIC;

    if ( json.HasMember( "mythic" ) )
      item.parsed.data.type_flags |= RAID_TYPE_MYTHIC;

    if ( json.HasMember( "warforged" ) )
      item.parsed.data.type_flags |= RAID_TYPE_WARFORGED;

    if ( item.parsed.data.item_class == ITEM_CLASS_WEAPON )
    {
      if ( ! jsonequip.HasMember( "dmgrange" ) )
        throw( "weapon damage range" );

      if ( ! jsonequip.HasMember( "speed" ) )
        throw( "weapon speed" );
    }

    if ( jsonequip.HasMember( "reqskill" ) )
      item.parsed.data.req_skill = jsonequip[ "reqskill" ].GetInt();

    if ( jsonequip.HasMember( "reqskillrank" ) )
      item.parsed.data.req_skill_level = jsonequip[ "reqskillrank" ].GetInt();

    // Todo binding type, needs htmlTooltip parsing
    if ( item.parsed.data.item_class == ITEM_CLASS_WEAPON )
    {
      item.parsed.data.delay = jsonequip[ "speed" ].GetDouble() * 1000.0;
      item.parsed.data.dmg_range = jsonequip[ "dmgrange" ].GetDouble();
    }

    int races = -1;
    if ( jsonequip.HasMember( "races" ) )
      races = jsonequip[ "races" ].GetInt();
    item.parsed.data.race_mask = races;

    int classes = -1;
    if ( jsonequip.HasMember( "classes" ) )
      classes = jsonequip[ "classes" ].GetInt();
    item.parsed.data.class_mask = classes;

    size_t n = 0;
    stat_e hybrid_stat = STAT_NONE;
    for ( rapidjson::Value::ConstMemberIterator i = jsonequip.MemberBegin(); 
          i != jsonequip.MemberEnd() && n < sizeof_array( item.parsed.data.stat_type_e ); i++ )
    {
      stat_e type = util::parse_stat_type( i -> name.GetString() );
      // wowhead josnEquip contains redundant entries for queries, take note so we can purge
      if ( type == STAT_STR_AGI || type == STAT_STR_INT || type == STAT_AGI_INT )
        hybrid_stat = type;
    }

    for ( rapidjson::Value::ConstMemberIterator i = jsonequip.MemberBegin(); 
          i != jsonequip.MemberEnd() && n < sizeof_array( item.parsed.data.stat_type_e ); i++ )
    {
      stat_e type = util::parse_stat_type( i -> name.GetString() );
      if ( type == STAT_NONE || type == STAT_ARMOR || util::translate_stat( type ) == ITEM_MOD_NONE ) 
        continue;

      // If we have a hybrid stat, don't record the excess STR/INT/AGI entries
      if ( hybrid_stat != STAT_NONE && ( type == STAT_STRENGTH || type == STAT_INTELLECT || type == STAT_AGILITY ) )
        continue;

      item.parsed.data.stat_type_e[ n ] = util::translate_stat( type );
      item.parsed.data.stat_val[ n ] = i -> value.GetInt();
      n++;

      // Soo, weapons need a flag to indicate caster weapon for correct DPS calculation.
      if ( item.parsed.data.delay > 0 && ( 
           item.parsed.data.stat_type_e[ n - 1 ] == ITEM_MOD_INTELLECT || 
           item.parsed.data.stat_type_e[ n - 1 ] == ITEM_MOD_SPIRIT ||
           item.parsed.data.stat_type_e[ n - 1 ] == ITEM_MOD_SPELL_POWER ) )
        item.parsed.data.flags_2 |= ITEM_FLAG2_CASTER_WEAPON;
    }
    
    int n_sockets = 0;
    if ( jsonequip.HasMember( "nsockets" ) )
      n_sockets = jsonequip[ "nsockets" ].GetUint();
    
    assert( n_sockets <= static_cast< int >( sizeof_array( item.parsed.data.socket_color ) ) );
    for ( int i = 0; i < n_sockets; i++ )
    {
      std::string socket_str = str::format( "socket%d", i + 1 );
      if ( jsonequip.HasMember( socket_str.c_str() ) )
        item.parsed.data.socket_color[ i ] = jsonequip[ socket_str.c_str() ].GetUint();
    }

    if ( jsonequip.HasMember( "socketbonus" ) )
      item.parsed.data.id_socket_bonus = jsonequip[ "socketbonus" ].GetUint();

    if ( jsonequip.HasMember( "itemset" ) )
      item.parsed.data.id_set =  std::abs( jsonequip[ "itemset" ].GetInt() );

    // Sad to the face
    std::string htmltooltip;
    xml -> get_value( htmltooltip, "htmlTooltip/cdata" );

    // Parse out Equip: and On use: strings
    int spell_idx = 0;

    std::shared_ptr<xml_node_t> htmltooltip_xml = xml_node_t::create( item.sim, htmltooltip );
    //htmltooltip_xml -> print( item.sim -> output_file, 2 );
    std::vector<xml_node_t*> spell_links = htmltooltip_xml -> get_nodes( "span" );
    for ( size_t i = 0; i < spell_links.size(); i++ )
    {
      int trigger_type = -1;
      unsigned spell_id = 0;

      std::string v;
      if ( spell_links[ i ] -> get_value( v, "." ) && v != "Equip: " && v != "Use: " )
        continue;

      if ( v == "Use: " )
        trigger_type = ITEM_SPELLTRIGGER_ON_USE;
      else if ( v == "Equip: " )
        trigger_type = ITEM_SPELLTRIGGER_ON_EQUIP;

      std::string url;
      if ( ! spell_links[ i ] -> get_value( url, "a/href" ) )
        continue;

      size_t begin = url.rfind( "=" );
      if ( begin == std::string::npos )
        continue;
      else
        begin++;

      spell_id = util::to_unsigned( url.substr( begin ) );
      if ( spell_id > 0 && trigger_type != -1 )
      {
        item.parsed.data.id_spell[ spell_idx ] = spell_id;
        item.parsed.data.trigger_spell[ spell_idx ] = trigger_type;
        spell_idx++;
      }
    }
  }
  catch ( const char* fieldname )
  {
    std::string error_str;

    xml -> get_value( error_str, "error/." );

    if ( caching != cache::ONLY )
      item.sim -> errorf( "Wowhead (%s): Player %s unable to parse item '%u' %s in slot '%s': %s\n",
                          source_desc_str( source ).c_str(), item.player -> name(), item.parsed.data.id,
                          fieldname, item.slot_name(), error_str.c_str() );
    return false;
  }

  return true;
}

// wowhead::download_item ===================================================

bool wowhead::download_item( item_t&            item,
                             wowhead_e          source,
                             cache::behavior_e  caching )
{
  bool ret = download_item_data( item, caching, source );

  if ( ret )
    item.source_str = "Wowhead";

  return ret;
}

std::string wowhead::decorated_spell_name( const std::string& name,
                                           unsigned spell_id,
                                           const std::string& spell_name,
                                           wowhead_e domain,
                                           const std::string& href_parm,
                                           bool affix )
{
  std::string decorated_name, base_href, prefix, suffix;

  bool notooltip = dual_tooltip_disable( spell_id );
  if ( spell_id > 1 && !notooltip ) 
  {
    base_href = "http://" + domain_str( domain ) + ".wowhead.com/spell=" + util::to_string( spell_id );

    if ( affix )
    {
      std::string::size_type affix_offset = name.find( spell_name );

      // Add an affix to the name, if the name does not match the 
      // spell name. Affix is either the prefix- or suffix portion of the 
      // non matching parts of the stats name.
      if ( affix_offset != std::string::npos && spell_name != name )
      {
        // Suffix
        if ( affix_offset == 0 )
          suffix += " (" + name.substr( spell_name.size() ) + ")";
        // Prefix
        else if ( affix_offset > 0 )
          prefix += " (" + name.substr( 0, affix_offset ) + ")";
      }
      else if ( affix_offset == std::string::npos )
        suffix += " (" + name + ")";
    }
  }

  if ( ! base_href.empty() )
  {
    if ( ! prefix.empty() )
      decorated_name += prefix + "&#160;";

    decorated_name += "<a href=\"" + base_href + "\" " + href_parm.c_str() + ">" + name + "</a>";

    if ( ! suffix.empty() )
      decorated_name += suffix;
  }
  else
    decorated_name = name;

  return decorated_name;
}

std::string wowhead::decorated_action_name( const std::string& name,
                                            action_t* action,
                                            wowhead_e domain, 
                                            const std::string& href_parm,
                                            bool affix )
{
  std::string spell_name;
  unsigned spell_id = 0;
  if ( ! action )
    return name;

  // Kludge auto attack decorations
  attack_t* a = dynamic_cast< attack_t* >( action );
  if ( a && a -> auto_attack )
  {
    spell_id = 6603;
    if ( a -> weapon )
      spell_name = "Auto Attack";
  }
  else
  {
    spell_id = action -> id;
    if ( spell_id > 1 )
      spell_name = action -> s_data -> name_cstr();
  }

  util::tokenize( spell_name );

  return decorated_spell_name( name, spell_id, spell_name, domain, href_parm, affix );
}

std::string wowhead::decorated_buff_name( const std::string& name,
                                          buff_t* buff,
                                          wowhead_e domain, 
                                          const std::string& href_parm,
                                          bool affix )
{
  std::string spell_name;
  unsigned spell_id = 0;

  if ( buff -> data().id() > 0 )
  {
    spell_id = buff -> data().id();
    spell_name = buff -> data().name_cstr();
  }

  util::tokenize( spell_name );

  return decorated_spell_name( name, spell_id, spell_name, domain, href_parm, affix );
}

std::string wowhead::domain_str( wowhead_e domain )
{
  switch ( domain )
  {
    case PTR: return "ptr";
#if SC_BETA
    case BETA: return SC_BETA_STR;
#endif
    default: return "www";
  }
}
