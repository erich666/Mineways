@rem echo Script to make JG-RTX terrainExt files. By Eric Haines, 11/21/2022 

@rem echo Setup: Copy this file to the TileMaker directory. Or, alternately (to keep that directory clean), create some subdirectory, say "MinewaysJGRTX" and copy this script into it. From the Mineways TileMaker directory, also copy the ChannelMixer.exe, TileMaker.exe, and terrainBase.png to your new directory MinewaysJGRTX. 

@rem Go to https://github.com/jasonjgardner/jg-rtx. Under the green Code button, Download ZIP. Unzip into MinewaysJGRTX, so that there's a directory called "jg-rtx-main" inside this directory (which has all the JG-RTX files in it). Then, use CMD and go to MinewaysJGRTX and run this script!

@rem echo The commands below encode tips from Jason Gardner: Use ./bedrock/pack/RP as your input path. Copy some of the missing textures from ./java/assets/minecraft/textures/block/ first. Then any remaining missing textures might be in ./mineways/textures/ – though some will be wonky.

rmdir /S /Q blocks_jgrtx
mkdir blocks_jgrtx

@rem Grab in least to most reliable (in theory) order from the variants 

@rem take the variant only if it's the only thing that exists
ChannelMixer.exe -i jg-rtx-main/bedrock -o blocks_jgrtx -v > bedrock_variants.log

@rem echo suppress metallic output for java and see if that gets rid of the weirdo blocks_jgrtx we later delete
ChannelMixer.exe -i jg-rtx-main/java -o blocks_jgrtx -v -k m > java.log

@rem favor RP over addon or variety (e.g., modern) variants, and over Java
@rem Java is suspect - there are mismatches such as gold_block_n/normal.png, with Bedrock being better
ChannelMixer.exe -i jg-rtx-main/bedrock/pack/RP -o blocks_jgrtx -v > bedrock.log

ChannelMixer.exe -i jg-rtx-main/mineways -o blocks_jgrtx -v > mineways.log

@rem these glass textures are much cooler - comment out if you just want the usual ones
ChannelMixer.exe -i jg-rtx-main/bedrock/variety/glass/subpacks/medieval_glass/textures/blocks -o blocks_jgrtx -v > medieval.log

@rem other alternate textures to explore: jg-rtx-main\bedrock\variety\modern\textures\blocks

@rem some tiles with the same names, need to get the good one:
copy /Y jg-rtx-main\java\pack\assets\minecraft\textures\block\grass.png blocks_jgrtx

@rem the redstone torch on has a crummy emissive texture - too dark: redstone_torch_on_e.png - copy regular torch, or open in Irfanview and do Shift+U (Image -> Auto-adjust colors) then save Ctrl+S.
copy /Y blocks_jgrtx\torch_on_e.png blocks_jgrtx\redstone_torch_on_e.png

@rem this one's a bad PNG, use the TGA instead that should be left after deleting this one
del blocks_jgrtx\amethyst_cluster.png 

@rem echo Remove lava_still_e.png and lava_flow_e.png, as they're too dim. The color texture will get used instead. Other textures are either too blackish or don't look good to me.
del blocks_jgrtx\lava_still_e.png
del blocks_jgrtx\lava_flow_e.png
del blocks_jgrtx\lava_still_r.png
del blocks_jgrtx\lava_flow_r.png
del blocks_jgrtx\lava_flow_m.png
del blocks_jgrtx\water_flow_m.png
del blocks_jgrtx\dispenser_front_horizontal_m.png
@rem not sure why I wanted to delete this one, looks OK now: del blocks_jgrtx\crying_obsidian_m.png
@rem there's a _normal Mineways and _n Java version of these - the _n version does nothing
@rem - don't need to delete the Java version, as -fnormal will favor the Mineways version: del blocks_jgrtx\piston_top_sticky_n.png
@rem echo there are a bunch of blocks_jgrtx with tiny mild flecks of metal in them - I wouldn't bother
@rem del blocks_jgrtx\deepslate_diamond_ore_m.png

@rem the java _n.png files are not to be trusted, the bedrock _normal.png files are better, so use -fnormal
@rem for example, piston_top_sticky_n.png has no normals. Others are "rotated" (barrel block, frogspawn).
TileMaker.exe -v -m -i terrainExt.png -d blocks_jgrtx -o terrainExt_JG-RTX64.png -fnormal -t 64 > tilemaker64.log
TileMaker.exe -v -m -i terrainExt.png -d blocks_jgrtx -o terrainExt_JG-RTX256.png -fnormal -t 256 > tilemaker256.log

@rem echo ============= debug stuff
@rem echo DEBUG: separate blocks_jgrtx to look through

@rem echo ChannelMixer.exe -i jg-rtx-main/java -o blocks_java

@rem echo ChannelMixer.exe -i jg-rtx-main/bedrock -o blocks_bedrock

@rem echo ChannelMixer.exe -i jg-rtx-main/mineways -o blocks_mineways

@rem echo debug string for Visual Studio run of tilemaker for debugging things: put input into blocks_test directory
@rem echo -i C:\Users\ehaines\Documents\_documents\JG-RTXmaster\BASEterrainExt_JG-RTX64.png -d C:\Users\ehaines\Documents\_documents\JG-RTXmaster\blocks_test -o C:\Users\ehaines\Documents\_documents\JG-RTXmaster\DEBUGterrainExt_JG-RTX64.png -t 64 

