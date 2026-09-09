#pragma once

// The public cheat source indices belong to core/Pad.cpp. Categories only
// reorder their presentation; activation, toggles and vehicle selection all
// use the original source index. test-cheat-menu.ps1 checks this table against
// the actual Pad.cpp names so additions or reordered cheats cannot drift.
namespace VrCheatMenu {
enum Category {
	GENERAL, WANTED, WEAPONS, APPEARANCE, WEATHER, VEHICLE_SELECTOR,
	SPECIAL_VEHICLES, VEHICLE_EFFECTS, PEDESTRIANS, CATEGORY_COUNT
};

static const unsigned char kSourceCategories[] = {
	VEHICLE_SELECTOR, // SPAWN NEXT CAR
	VEHICLE_SELECTOR, // SPAWN NEXT MOTORCYCLE
	VEHICLE_SELECTOR, // SPAWN NEXT HELICOPTER
	VEHICLE_SELECTOR, // SPAWN NEXT AIRPLANE
	VEHICLE_SELECTOR, // SPAWN NEXT BOAT
	GENERAL,          // GOD MODE
	GENERAL,          // RUN WITHOUT LIMITS
	GENERAL,          // OPEN STORY BRIDGES
	WANTED,           // UNLOCK 6-STAR WANTED CAP
	WANTED,           // NEVER WANTED
	WEAPONS,          // WEAPONS SET 1
	WEAPONS,          // WEAPONS SET 2
	WEAPONS,          // WEAPONS SET 3
	GENERAL,          // RESTORE HEALTH
	GENERAL,          // RESTORE ARMOUR
	GENERAL,          // ADD 250000 MONEY
	WANTED,           // RAISE WANTED
	WANTED,           // CLEAR WANTED
	SPECIAL_VEHICLES, // SPAWN RHINO
	SPECIAL_VEHICLES, // SPAWN BLOODRING 1
	SPECIAL_VEHICLES, // SPAWN BLOODRING 2
	SPECIAL_VEHICLES, // SPAWN ROMERO
	SPECIAL_VEHICLES, // SPAWN LOVE FIST LIMO
	SPECIAL_VEHICLES, // SPAWN TRASHMASTER
	SPECIAL_VEHICLES, // SPAWN SABRE TURBO
	SPECIAL_VEHICLES, // SPAWN CADDY
	SPECIAL_VEHICLES, // SPAWN HOTRING 1
	SPECIAL_VEHICLES, // SPAWN HOTRING 2
	VEHICLE_EFFECTS,  // BLOW UP ALL CARS
	APPEARANCE,       // RANDOM PLAYER OUTFIT
	APPEARANCE,       // PLAYER LANCE
	APPEARANCE,       // PLAYER CANDY
	APPEARANCE,       // PLAYER KEN
	APPEARANCE,       // PLAYER HILARY
	APPEARANCE,       // PLAYER JEZZ
	APPEARANCE,       // PLAYER PHIL
	APPEARANCE,       // PLAYER SONNY
	APPEARANCE,       // PLAYER MERCEDES
	APPEARANCE,       // PLAYER DICK
	APPEARANCE,       // PLAYER DIAZ
	APPEARANCE,       // PLAYER FAT
	APPEARANCE,       // PLAYER THIN
	PEDESTRIANS,      // PEDESTRIAN RIOT
	PEDESTRIANS,      // EVERYONE ATTACKS PLAYER
	PEDESTRIANS,      // ARMED PEDESTRIANS
	PEDESTRIANS,      // CHICKS WITH GUNS
	PEDESTRIANS,      // FANNY MAGNET
	PEDESTRIANS,      // PICK UP NEARBY CHICK
	WEATHER,          // EXTRA SUNNY WEATHER
	WEATHER,          // SUNNY WEATHER
	WEATHER,          // CLOUDY WEATHER
	WEATHER,          // RAINY WEATHER
	WEATHER,          // FOGGY WEATHER
	WEATHER,          // FAST WEATHER
	WEATHER,          // FASTER GAME
	WEATHER,          // SLOWER GAME
	VEHICLE_EFFECTS,  // WHEELS ONLY
	VEHICLE_EFFECTS,  // FLYING CARS
	VEHICLE_EFFECTS,  // CARS DRIVE ON WATER
	VEHICLE_EFFECTS,  // FLYING BOATS
	VEHICLE_EFFECTS,  // STRONG GRIP
	VEHICLE_EFFECTS,  // BIG WHEELS
	VEHICLE_EFFECTS,  // ALL GREEN LIGHTS
	VEHICLE_EFFECTS,  // AGGRESSIVE TRAFFIC
	VEHICLE_EFFECTS,  // BLACK CARS
	VEHICLE_EFFECTS,  // PINK CARS
	GENERAL,          // SHOW CHASE STATS
	GENERAL,          // SMOKING TOMMY
	GENERAL           // SUICIDE
};

inline const char *CategoryName(int category)
{
	switch(category){
	case GENERAL: return "GENERAL / PLAYER";
	case WANTED: return "WANTED / POLICE";
	case WEAPONS: return "WEAPONS";
	case APPEARANCE: return "PLAYER APPEARANCE";
	case WEATHER: return "WEATHER / TIME";
	case VEHICLE_SELECTOR: return "VEHICLE SELECTOR";
	case SPECIAL_VEHICLES: return "SPECIAL VEHICLES";
	case VEHICLE_EFFECTS: return "VEHICLE EFFECTS";
	case PEDESTRIANS: return "PEDESTRIANS";
	default: return "CHEATS";
	}
}

inline int CategoryForSource(int source)
{
	if(source < 0) return -1;
	// New backend cheats remain reachable even before they are categorised.
	return source < (int)sizeof(kSourceCategories) ?
		kSourceCategories[source] : GENERAL;
}

inline int Count(int category, int sourceCount)
{
	if(category < 0 || category >= CATEGORY_COUNT) return 0;
	int count = 0;
	for(int source = 0; source < sourceCount; ++source)
		if(CategoryForSource(source) == category) ++count;
	return count;
}

inline int SourceIndex(int category, int item, int sourceCount)
{
	if(category < 0 || category >= CATEGORY_COUNT || item < 0) return -1;
	for(int source = 0; source < sourceCount; ++source)
		if(CategoryForSource(source) == category && item-- == 0)
			return source;
	return -1;
}
}
