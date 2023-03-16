@rem echo Script to make JG-RTX terrainExt files. By Eric Haines, 11/21/2022 

@rem echo Setup: Create some subdirectory, say "MinewaysJGRTX". From the Mineways TileMaker directory copy the ChannelMixer.exe, TileMaker.exe, and terrainBase.png to MinewaysJGRTX. Go to https://github.com/jasonjgardner/jg-rtx. Under the green Code button, Download ZIP. Unzip into MinewaysJGRTX, so that there's a directory called "jg-rtx-main" inside this directory (which has all the JG-RTX files in it). Then, use CMD and go to MinewaysJGRTX and run this script!

@rem echo The commands below encode tips from Jason Gardner: Use ./bedrock/pack/RP as your input path. Copy some of the missing textures from ./java/assets/minecraft/textures/block/ first. Then any remaining missing textures might be in ./mineways/textures/ – though some will be wonky.

rmdir /S /Q blocks
mkdir blocks

@rem echo suppress metallic output for java and see if that gets rid of the weirdo blocks we later delete
ChannelMixer.exe -i jg-rtx-main/java -o blocks -v -k m > java.log

ChannelMixer.exe -i jg-rtx-main/bedrock -o blocks -v > bedrock.log

ChannelMixer.exe -i jg-rtx-main/mineways -o blocks -v > mineways.log

@rem echo Remove lava_still_e.png and lava_flow_e.png, as they're too dim. The color texture will get used instead.
del blocks\lava_still_e.png
del blocks\lava_flow_e.png
del blocks\lava_still_r.png
del blocks\lava_flow_r.png
del blocks\lava_flow_m.png
del blocks\water_flow_m.png
del blocks\dispenser_front_horizontal_m.png
del blocks\obsidian_m.png
del blocks\crying_obsidian_m.png
del blocks\piston_top_sticky_n.png
@rem echo there are a bunch of blocks with tiny mild flecks of metal in them - I wouldn't bother
del blocks\deepslate_diamond_ore_m.png

TileMaker.exe -v -m -i terrainBase.png -d blocks -o terrainExt_JG-RTX64.png -t 64 > tilemaker64.log
TileMaker.exe -v -m -i terrainBase.png -d blocks -o terrainExt_JG-RTX256.png -t 256 > tilemaker256.log

@rem echo ============= debug stuff
@rem echo DEBUG: separate blocks to look through

@rem echo ChannelMixer.exe -i jg-rtx-main/java -o blocks_java

@rem echo ChannelMixer.exe -i jg-rtx-main/bedrock -o blocks_bedrock

@rem echo ChannelMixer.exe -i jg-rtx-main/mineways -o blocks_mineways

@rem echo debug string for Visual Studio run of tilemaker for debugging things: put input into blocks_test directory
@rem echo -i C:\Users\ehaines\Documents\_documents\JG-RTXmaster\BASEterrainExt_JG-RTX64.png -d C:\Users\ehaines\Documents\_documents\JG-RTXmaster\blocks_test -o C:\Users\ehaines\Documents\_documents\JG-RTXmaster\DEBUGterrainExt_JG-RTX64.png -t 64 

