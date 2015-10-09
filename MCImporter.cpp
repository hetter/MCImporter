
#include "MCImporter.h"
#include <string>
#include <sstream>
#include <fstream>
#include "INPLRuntimeState.h"
#include "NPLInterface.hpp"
#include "mc/cache.h"
#include "MCblock.h"

using namespace ParaEngine;

#define TEMP_MCR_FILENAME "temp/mcimporter.mcr.tmp"

#pragma region PE_DLL 

#ifdef WIN32
#define CORE_EXPORT_DECL    __declspec(dllexport)
#else
#define CORE_EXPORT_DECL
#endif

// forware declare of exported functions. 
#ifdef __cplusplus
extern "C" {
#endif
	CORE_EXPORT_DECL const char* LibDescription();
	CORE_EXPORT_DECL int LibNumberClasses();
	CORE_EXPORT_DECL unsigned long LibVersion();
	CORE_EXPORT_DECL ParaEngine::ClassDescriptor* LibClassDesc(int i);
	CORE_EXPORT_DECL void LibInit();
	CORE_EXPORT_DECL void LibActivate(int nType, void* pVoid);
	CORE_EXPORT_DECL bool LoadMCWorld(const std::string& sFolderName);
	CORE_EXPORT_DECL bool GetRegionBlocks(int x, int z, std::vector<int> *blocks);
#ifdef __cplusplus
}   /* extern "C" */
#endif

HINSTANCE Instance = NULL;



ClassDescriptor* MCImporter_GetClassDesc();
typedef ClassDescriptor* (*GetClassDescMethod)();

GetClassDescMethod Plugins[] = 
{
	MCImporter_GetClassDesc,
};

#define MCImporter_CLASS_ID Class_ID(0x2b305a29, 0x47a409ce)

class MCImporterDesc:public ClassDescriptor
{
public:
	void* Create(bool loading = FALSE)
	{
		return new MCImporter();
	}

	const char* ClassName()
	{
		return "IMCImporter";
	}

	SClass_ID SuperClassID()
	{
		return OBJECT_MODIFIER_CLASS_ID;
	}

	Class_ID ClassID()
	{
		return MCImporter_CLASS_ID;
	}

	const char* Category() 
	{ 
		return "Model Importer"; 
	}

	const char* InternalName() 
	{ 
		return "Model Importer"; 
	}

	HINSTANCE HInstance() 
	{ 
		extern HINSTANCE Instance;
		return Instance; 
	}
};

ClassDescriptor* MCImporter_GetClassDesc()
{
	static MCImporterDesc s_desc;
	return &s_desc;
}

CORE_EXPORT_DECL const char* LibDescription()
{
	return "ParaEngine MCImporter Ver 1.0.0";
}

CORE_EXPORT_DECL unsigned long LibVersion()
{
	return 1;
}

CORE_EXPORT_DECL int LibNumberClasses()
{
	return sizeof(Plugins)/sizeof(Plugins[0]);
}

CORE_EXPORT_DECL ClassDescriptor* LibClassDesc(int i)
{
	if (i < LibNumberClasses() && Plugins[i])
	{
		return Plugins[i]();
	}
	else
	{
		return NULL;
	}
}

CORE_EXPORT_DECL void LibInit()
{
}
#ifdef WIN32
BOOL WINAPI DllMain(HINSTANCE hinstDLL,ULONG fdwReason,LPVOID lpvReserved)
#else
void __attribute__ ((constructor)) DllMain()
#endif
{
	// TODO: dll start up code here
#ifdef WIN32
	Instance = hinstDLL;				// Hang on to this DLL's instance handle.
	return (TRUE);
#endif
}
#pragma endregion PE_DLL 

MCImporter::MCImporter()
{
	MCBlock::StaticInit();
}

MCImporter::~MCImporter()
{

}

MCImporter& MCImporter::GetSingleton()
{
	static MCImporter g_singleton;
	return g_singleton;
}

MCImporter& MCImporter::CreateGetSingleton()
{
#ifdef _STATIC_MCIMPORTER_OBJECT_
	return g_singleton;
#endif // _STATIC_MCIMPORTER_OBJECT_

#ifndef _STATIC_MCIMPORTER_OBJECT_
#define _STATIC_MCIMPORTER_OBJECT_
	static MCImporter g_singleton;
	return g_singleton;
#endif
}

bool MCImporter::LoadWorld( const std::string& sFolderName )
{
	if(m_world.load(sFolderName))
	{
		m_world_cache = std::unique_ptr<mc::WorldCache>(new mc::WorldCache(m_world));
		return true;
	}
	return false;
}

uint16_t MCImporter::getBlock( const mc::BlockPos& pos, mc::Chunk* chunk ) const
{
	if (pos.y < 0)
		return 0;
	mc::ChunkPos chunk_pos(pos);
	mc::Chunk* mychunk = chunk;
	if (chunk == NULL || chunk_pos != chunk->getPos())
		mychunk = m_world_cache->getChunk(chunk_pos);
	// chunk may be NULL
	if (mychunk == NULL) {
		return 0;
		// otherwise get id and data
	} else {
		mc::LocalBlockPos local(pos);
		uint16_t id = mychunk->getBlockID(local);
		// assume that air does not have any data
		if (id == 0)
			return 0;
		return id;
	}
}

/** TODO: this function is not correct. 
block that is surrounded by non-opache blocks may be wrongly removed. 
*/
bool MCImporter::isOccludedBlock( const mc::BlockPos& pos, mc::Chunk* chunk, uint16_t id ) const
{
	// uint16_t north, south, east, west, top, bottom;
	if( !MCBlock::IsSolidBlock(getBlock(pos + mc::DIR_TOP, chunk)) ||
		!MCBlock::IsSolidBlock(getBlock(pos + mc::DIR_BOTTOM, chunk)) ||
		!MCBlock::IsSolidBlock(getBlock(pos + mc::DIR_NORTH, chunk)) || 
		!MCBlock::IsSolidBlock(getBlock(pos + mc::DIR_SOUTH, chunk)) ||
		!MCBlock::IsSolidBlock(getBlock(pos + mc::DIR_EAST, chunk)) ||
		!MCBlock::IsSolidBlock(getBlock(pos + mc::DIR_WEST, chunk)))
	{
		return false;
	}
	return true;
}

bool LoadMCWorld(const std::string& sFolderName)
{
	MCImporter& mc_importer = MCImporter::CreateGetSingleton();
	if (!sFolderName.empty())
	{
		if (mc_importer.m_world.load(sFolderName))
		{
			mc_importer.m_world_cache = std::unique_ptr<mc::WorldCache>(new mc::WorldCache(mc_importer.m_world));
			return true;
		}
		return false;
	}
	return false;
}

bool GetRegionBlocks(int x, int z, std::vector<int> *blocks)
{
	//LoadMCWorld("F:/game/Minecraft1.8.8/.minecraft/saves/test");
	MCImporter& mc_importer = MCImporter::CreateGetSingleton();

	double regionCount = (double)(mc_importer.m_world.getRegionCount());
	auto regions = mc_importer.m_world.getAvailableRegions();
	int count = 0;
	int i = 0;
	mc::RegionFile* region = mc_importer.m_world_cache->getRegion(mc::RegionPos(x,z));
	if (!region){
		return false;
	}

	uint16_t min_y = 0;
	uint16_t max_y = 256;

	auto region_chunks = region->getContainingChunks();

	// go through all chunks in the region
	for (auto chunk_it = region_chunks.begin(); chunk_it != region_chunks.end(); ++chunk_it)
	{
		const mc::ChunkPos& chuck_pos = *chunk_it;
		mc::Chunk* mychunk = mc_importer.m_world_cache->getChunk(chuck_pos);
		if (mychunk)
		{
			for (int x = 0; x < 16; ++x)
			{
				for (int z = 0; z < 16; ++z)
				{
					for (int y = min_y; y < max_y; ++y)
					{
						mc::LocalBlockPos pos(x, z, y);
						uint16_t block_id = mychunk->getBlockID(pos);
						uint16_t block_data = mychunk->getBlockData(pos);
						if (block_id != 0)
						{
							BlockPos gpos = pos.toGlobalPos(chuck_pos);
							blocks->push_back(gpos.x);
							blocks->push_back(gpos.y);
							blocks->push_back(gpos.z);
							
							if (MCBlock::TranslateMCBlock(block_id, block_data))
							{
								char sMsg[130];
								snprintf(sMsg, 100, "mc region x=%d,z=%d,block_x=%d,block_y=%d,block_z=%d,block_id=%d translate failed!;", x, z, gpos.x,gpos.y,gpos.z,block_id);
							}
							blocks->push_back((int)block_id);
							blocks->push_back((int)block_data);
						}
					}
				}
			}
		}
	}
	return true;
}

CORE_EXPORT_DECL void LibActivate(int nType, void* pVoid)
{
	if(nType == ParaEngine::PluginActType_STATE)
	{
		NPL::INPLRuntimeState* pState = (NPL::INPLRuntimeState*)pVoid;
		const char* sMsg = pState->GetCurrentMsg();
		int nMsgLength = pState->GetCurrentMsgLength();

		NPLInterface::NPLObjectProxy tabMsg = NPLInterface::NPLHelper::MsgStringToNPLTable(sMsg);
		const std::string& sCmd = tabMsg["cmd"];

		MCImporter& mc_importer = MCImporter::GetSingleton();

		if(sCmd == "load")
		{
			const std::string& sFolder = tabMsg["folder"];
			uint16_t min_y = 0;
			uint16_t max_y = 256;
			if(tabMsg["min_y"].GetType() == NPLInterface::NPLObjectBase::NPLObjectType_Number)
			{
				min_y = (uint16_t)((double)(tabMsg["min_y"]));
			}
			if(tabMsg["max_y"].GetType() == NPLInterface::NPLObjectBase::NPLObjectType_Number)
			{
				max_y = (uint16_t)((double)(tabMsg["max_y"]));
			}
			bool bExportOpaque = true;
			if(tabMsg["bExportOpaque"].GetType() == NPLInterface::NPLObjectBase::NPLObjectType_Bool)
			{
				bExportOpaque = (bool)tabMsg["bExportOpaque"];
			}
			
			if(!sFolder.empty())
			{
				if(mc_importer.LoadWorld(sFolder))
				{
					NPLInterface::NPLObjectProxy msg;
					msg["succeed"] = "true";
					msg["region_count"] = (double)(mc_importer.m_world.getRegionCount());
					
					NPLInterface::NPLObjectProxy regions_table;

					std::ofstream file(TEMP_MCR_FILENAME, std::ios_base::binary);

					if(!file.is_open())
					{
						return;
					}
					// go through all regions in the world
					auto regions = mc_importer.m_world.getAvailableRegions();
					int count = 0;
					int i=0;
					for (auto itCur = regions.cbegin(); itCur != regions.cend(); itCur++)
					{
						NPLInterface::NPLObjectProxy region_table;
						region_table["x"] = (double) (itCur->x);
						region_table["z"] = (double) (itCur->z);
						std::stringstream sIndexStream;
						std::string sIndex; 
						sIndexStream << i;
						sIndexStream >> sIndex;
						regions_table[sIndex] = region_table;

						{
							file << "region";
							file << ",";
							file << itCur->x;
							file << ",";
							file << itCur->z;
							file << ",";
							file << 0;
							file << "\n";
						}
						

						mc::RegionFile* region = mc_importer.m_world_cache->getRegion(*itCur);
						if(!region){
							continue;
						}
						auto region_chunks = region->getContainingChunks();

						// go through all chunks in the region
						for (auto chunk_it = region_chunks.begin(); chunk_it != region_chunks.end(); ++chunk_it) 
						{
							const mc::ChunkPos& chuck_pos = *chunk_it;
							mc::Chunk* mychunk = mc_importer.m_world_cache->getChunk(chuck_pos);
							if(mychunk)
							{
								for(int x=0;x<16; ++x)
								{
									for(int z=0;z<16; ++z)
									{
										for(int y=min_y;y<max_y; ++y)
										{
											mc::LocalBlockPos pos(x,z,y);
											uint16_t block_id = mychunk->getBlockID(pos);
											uint16_t block_data = mychunk->getBlockData(pos);
											if(block_data>0)
												block_id = block_id*100+block_data;

											if(block_id!=0)
											{
												BlockPos gpos = pos.toGlobalPos(chuck_pos);
												if( bExportOpaque || !mc_importer.isOccludedBlock(gpos, mychunk, block_id))
												{
													file << gpos.x;
													file << ",";
													file << gpos.y;
													file << ",";
													file << gpos.z;
													file << ",";
													file << block_id;
													file << "\n";
													count++;
												}
											}
										}
									}
								}
								file.flush();
							}
						}
					}
					msg["regions"] = regions_table;
					msg["filename"] = TEMP_MCR_FILENAME;
					msg["count"] = (double)count;
					file.close();
					std::string output;
					NPLInterface::NPLHelper::NPLTableToString("msg", msg, output);
					pState->activate("script/apps/Aries/Creator/Game/Tasks/MCImporterTask.lua", output.c_str(), output.size());
				}
			}
		}
		else if(sCmd == "getregion")
		{
			int x = tabMsg["x"];
			int y = tabMsg["y"];
			RegionPos region_pos(x,y);
			if(mc_importer.m_world.hasRegion(region_pos))
			{
				NPLInterface::NPLObjectProxy msg;
				msg["succeed"] = "true";

				std::string output;
				NPLInterface::NPLHelper::NPLTableToString("msg", msg, output);
				pState->activate("script/apps/Aries/Creator/Game/Tasks/MCImporterTask.lua", output.c_str(), output.size());
			}
		}
	}
}

