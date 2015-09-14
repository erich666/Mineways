# Cycles Mineways setup
# Created by Jonathan Edelman
# Version 1.1.2, 9/13/15
# Copyright © 2015 Jonathan Edelman
# Please send suggestions or report bugs at jonathanedelman@mail.com
# This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation under version 3 of the License.
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details at http://www.gnu.org/licenses/gpl-3.0.en.html

# Distributed with Mineways, http://mineways.com

# How to use:
# First, you must edit this script a tiny bit. You need to change the PREFIX value listed in the
# CONSTANTS section. Change PREFIX="" to whatever your save file name is. For example, if it's
# castle.obj, then do this:
# PREFIX="castle"
# Optionally, you can change the other constants as described by the comments to modify the scene.
# Note that clouds and animation are not supported yet, and that the sky shader currently only handles day and night


# To use the script within Blender, for use with the Cycles renderer:

# To the right of the "Help" menu in the upper left, click on the "keys" icon next to the word
#"Default" and pick "Scripting". Optionally, you can simply change any window to the text editor.

# At the bottom of the gray window you'll see a menu "Text"; click it and select "Open Text
# Block". Go to the directory where this file "cycles_mineways.py" is and select it. You should now
# see some text in the gray window. Optionally, you can also paste in the text.

# To apply this script, click on the "Run Script" button at the bottom right of the text window.

# To see that the script did something, from the upper left select "Window" and "Toggle System Console".
# If you are running OS X, you will need to follow a different set of instructions to do this.
# Find you application, right click it, hit "Show Package Contents".
# Navigate to Contents/MacOS/blender
# Launch blender this way; this will show the terminal.
# It isn't critical to see this window, but gives you a warm and fuzzy feeling that the script has worked. It also helps provide debug info if something goes wrong.


#CONSTANTS


#The prefix of the texture files it uses
PREFIX=""
#If this is true, the script will request a scene to work with; otherwise, it will use all scenes
USER_INPUT_SCENE=False
#Cloud state, either True or False
CLOUD_STATE=False
#Changing the number here changes what water shader will be used, 0 to use the normal shader, 1 to use a partially transparent but still only textured shader, 2 for a choppy shader, 3 for a wavy shader
WATER_SHADER_TYPE=int(1)
#Changing the number here changes the type of sky shader used, 0 for no shader
SKY_SHADER_TYPE=int(0)
#Time of day–note that the decimal is not in minutes, and is a fraction (ex. 12:30 is 12.50)
TIME_OF_DAY=float(12.00)
#Decide if  lava is animated
LAVA_ANIMATION=False

#List of transparent blocks; old names for blocks are at the end of the list
transparentBlocks=["Acacia_Door","Acacia_Leaves","Activator_Rail","Bed","Birch_Door","Brewing_Stand","Brown_Mushroom","Cactus","Carrot","Cauldron","Cobweb","Cocoa","Dandelion","Dark_Oak_Door","Dead_Bush","Detector_Rail","Enchantment_Table","Glass","Glass_Pane","Grass","Iron_Bars","Iron_Door","Iron_Trapdoor","Jungle_Door","Lily_Pad","Melon_Stem","Monster_Spawner","Nether_Wart","Oak_Leaves","Oak_Sapling","Poppy","Potato","Powered_Rail","Pumpkin_Stem","Rail","Red_Mushroom","Redstone_Comparator_(off)","Redstone_Repeater_(off)","Redstone_Torch_(off)","Spruce_Door","Stained_Glass","Sugar_Cane","Sunflower","Trapdoor","Vines","Wheat","Wooden_Door","Acacia/Dark_Oak_Leaves","Carrots","Crops","Large_Flower","Leaves","Potatoes","Repeater_(off)","Sapling","Tall_Grass"]
#List of light emitting blocks; old names for blocks are at the end of the list
lightBlocks=["End_Rod","End_Portal","Ender_Chest","Glowstone","Jack_o'Lantern","Lava","Nether_Portal","Redstone_Lamp_(on)","Sea_Lantern","Stationary_Lava","Flowing_Lava"]
#List of light emitting and transparent block; old name for repeater block is at the end of the list
lightTransparentBlocks=["Beacon","Fire","Powered_Rail_(on)","Redstone_Comparator_(on)","Redstone_Repeater_(on)","Redstone_Torch_(on)","Torch","Repeater_(on)"]


#SHADERS

def Normal_Shader(material):
    #Make the material use nodes
    material.use_nodes=True
    #Set the variable node_tree to be the material's node tree and variable nodes to be the node tree's nodes
    node_tree=material.node_tree
    nodes=material.node_tree.nodes
    #Remove the old nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #commented out alt versions
    #node_tree=bpy.data.materials[material].node_tree
    #diffuse_node=nodes.get('ShaderNodeBsdfDiffuse')
    #Create the output node
    output_node=nodes.new('ShaderNodeOutputMaterial')
    output_node.location=(300,300)
    #Create the diffuse node
    diffuse_node=nodes.new('ShaderNodeBsdfDiffuse')
    diffuse_node.location=(0,300)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-300,300)
    rgba_node.label = "RGBA"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],diffuse_node.inputs["Color"])
    link=links.new(diffuse_node.outputs["BSDF"],output_node.inputs["Surface"])
    
def Transparent_Shader(material):
    #Make the material use nodes
    material.use_nodes=True
    #Set the variable node_tree to be the material's node tree and variable nodes to be the node tree's nodes
    node_tree=material.node_tree
    nodes=material.node_tree.nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #commented out alt versions
    #node_tree=bpy.data.materials[material].node_tree
    #diffuse_node=nodes.get('ShaderNodeBsdfDiffuse')
    #Create the output node
    output_node=nodes.new('ShaderNodeOutputMaterial')
    output_node.location=(300,300)
    #Create the mix shader
    mix_node=nodes.new('ShaderNodeMixShader')
    mix_node.location=(0,300)
    #Create the diffuse node
    diffuse_node=nodes.new('ShaderNodeBsdfDiffuse')
    diffuse_node.location=(-300,300)
    #Create the transparent node
    transparent_node=nodes.new('ShaderNodeBsdfTransparent')
    transparent_node.location=(-300,0)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-600,300)
    rgba_node.label = "RGBA"
    #Create the alpha node
    alpha_node=nodes.new('ShaderNodeTexImage')
    alpha_node.image = bpy.data.images[PREFIX+"-Alpha.png"]
    alpha_node.interpolation=('Closest')
    alpha_node.location=(-600,0)
    alpha_node.label = "Alpha"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],diffuse_node.inputs["Color"])
    link=links.new(alpha_node.outputs["Color"],mix_node.inputs["Fac"])
    link=links.new(transparent_node.outputs["BSDF"],mix_node.inputs[1])
    link=links.new(diffuse_node.outputs["BSDF"],mix_node.inputs[2])
    link=links.new(mix_node.outputs["Shader"],output_node.inputs["Surface"])
    
def Light_Emiting_Shader(material):
    #Make the material use nodes
    material.use_nodes=True
    #Set the variable node_tree to be the material's node tree and variable nodes to be the node tree's nodes
    node_tree=material.node_tree
    nodes=material.node_tree.nodes
    #Remove the old nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #commented out alt versions
    #node_tree=bpy.data.materials[material].node_tree
    #diffuse_node=nodes.get('ShaderNodeBsdfDiffuse')
    #Create the output node
    output_node=nodes.new('ShaderNodeOutputMaterial')
    output_node.location=(300,300)
    #Create the emission node
    emission_node=nodes.new('ShaderNodeEmission')
    emission_node.location=(0,300)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-300,300)
    rgba_node.label = "RGBA"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],emission_node.inputs["Color"])
    link=links.new(emission_node.outputs["Emission"],output_node.inputs["Surface"])
    if (material==bpy.data.materials.get("Stationary_Lava") or material==bpy.data.materials.get("Flowing_Lava")) and LAVA_ANIMATION==True:
        pass
    
def Transparent_Emiting_Shader(material):
    #Make the material use nodes
    material.use_nodes=True
    #Set the variable node_tree to be the material's node tree and variable nodes to be the node tree's nodes
    node_tree=material.node_tree
    nodes=material.node_tree.nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #commented out alt versions
    #node_tree=bpy.data.materials[material].node_tree
    #diffuse_node=nodes.get('ShaderNodeBsdfDiffuse')
    #Create the output node
    output_node=nodes.new('ShaderNodeOutputMaterial')
    output_node.location=(300,300)
    #Create the mix shader
    mix_node=nodes.new('ShaderNodeMixShader')
    mix_node.location=(0,300)
    #Create the diffuse node
    emission_node=nodes.new('ShaderNodeEmission')
    emission_node.location=(-300,300)
    #Create the transparent node
    transparent_node=nodes.new('ShaderNodeBsdfTransparent')
    transparent_node.location=(-300,0)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-600,300)
    rgba_node.label = "RGBA"
    #Create the alpha node
    alpha_node=nodes.new('ShaderNodeTexImage')
    alpha_node.image = bpy.data.images[PREFIX+"-Alpha.png"]
    alpha_node.interpolation=('Closest')
    alpha_node.location=(-600,0)
    alpha_node.label = "Alpha"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],emission_node.inputs["Color"])
    link=links.new(alpha_node.outputs["Color"],mix_node.inputs["Fac"])
    link=links.new(transparent_node.outputs["BSDF"],mix_node.inputs[1])
    link=links.new(emission_node.outputs["Emission"],mix_node.inputs[2])
    link=links.new(mix_node.outputs["Shader"],output_node.inputs["Surface"])

def Stationary_Water_Shader_1(material):
    #Make the material use nodes
    material.use_nodes=True
    #Set the variable node_tree to be the material's node tree and variable nodes to be the node tree's nodes
    node_tree=material.node_tree
    nodes=material.node_tree.nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #commented out alt versions
    #node_tree=bpy.data.materials[material].node_tree
    #diffuse_node=nodes.get('ShaderNodeBsdfDiffuse')
    #Create the output node
    output_node=nodes.new('ShaderNodeOutputMaterial')
    output_node.location=(300,300)
    #Create the mix shader
    mix_node=nodes.new('ShaderNodeMixShader')
    mix_node.location=(0,300)
    #Create the diffuse node
    diffuse_node=nodes.new('ShaderNodeBsdfDiffuse')
    diffuse_node.location=(-300,300)
    #Create the transparent node
    transparent_node=nodes.new('ShaderNodeBsdfTransparent')
    transparent_node.location=(-300,0)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-600,300)
    rgba_node.label = "RGBA"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],diffuse_node.inputs["Color"])
    link=links.new(transparent_node.outputs["BSDF"],mix_node.inputs[1])
    link=links.new(diffuse_node.outputs["BSDF"],mix_node.inputs[2])
    link=links.new(mix_node.outputs["Shader"],output_node.inputs["Surface"])
    
def Stationary_Water_Shader_2(material):
    #Make the material use nodes
    material.use_nodes=True
    #Set the variable node_tree to be the material's node tree and variable nodes to be the node tree's nodes
    node_tree=material.node_tree
    nodes=material.node_tree.nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #Create the output node
    output_node=nodes.new('ShaderNodeOutputMaterial')
    output_node.location=(300,300)
    #Create the first mix shader
    mix_node=nodes.new('ShaderNodeMixShader')
    mix_node.location=(0,300)
    mix_node.inputs["Fac"].default_value=(.1)
    #Create the second mix shader
    mix_node_two=nodes.new('ShaderNodeMixShader')
    mix_node_two.location=(-300,300)
    #Create the transparent node
    transparent_node=nodes.new('ShaderNodeBsdfTransparent')
    transparent_node.location=(-300,0)
    #Create the glossy node
    glossy_node=nodes.new('ShaderNodeBsdfGlossy')
    glossy_node.location=(-600,0)
    glossy_node.inputs["Roughness"].default_value=(0)
    #Create the refraction node
    refraction_node=nodes.new('ShaderNodeBsdfRefraction')
    refraction_node.location=(-600,300)
    refraction_node.inputs["IOR"].default_value=(1.333)
    #Create the mixrgb node
    mixrgb_node=nodes.new('ShaderNodeMixRGB')
    mixrgb_node.location=(-900,0)
    mixrgb_node.inputs["Color2"].default_value=(0,0,0,0)
    mixrgb_node.inputs["Fac"].default_value=(1)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-1200,0)
    rgba_node.label = "RGBA"
    #Create the first multiply node
    multiply_node=nodes.new('ShaderNodeMath')
    multiply_node.location=(0,-300)
    multiply_node.operation=('MULTIPLY')
    #Create the add node
    add_node=nodes.new('ShaderNodeMath')
    add_node.location=(-300,-300)
    add_node.operation=('ADD')
    #Create the first voronoi texture
    voronoi_node=nodes.new('ShaderNodeTexVoronoi')
    voronoi_node.location=(-600,-300)
    voronoi_node.inputs[1].default_value=(2.0)
    #Create the second multiply node
    multiply_node_two=nodes.new('ShaderNodeMath')
    multiply_node_two.location=(-600,-600)
    multiply_node_two.operation=('MULTIPLY')
    #Create the second voronoi texture
    voronoi_node_two=nodes.new('ShaderNodeTexVoronoi')
    voronoi_node_two.location=(-900,-600)
    voronoi_node_two.inputs[1].default_value=(3.0)
    #Create the texture coordinate node
    texture_coordinate_node=nodes.new('ShaderNodeTexCoord')
    texture_coordinate_node.location=(-1200,-300)
    #Link the nodes
    links=node_tree.links
    link=links.new(mix_node.outputs["Shader"],output_node.inputs["Surface"])
    link=links.new(transparent_node.outputs["BSDF"],mix_node.inputs[2])
    link=links.new(mix_node_two.outputs["Shader"],mix_node.inputs[1])
    link=links.new(refraction_node.outputs["BSDF"],mix_node_two.inputs[1])
    link=links.new(glossy_node.outputs["BSDF"],mix_node_two.inputs[2])
    link=links.new(mixrgb_node.outputs["Color"],glossy_node.inputs["Color"])
    link=links.new(rgba_node.outputs["Color"],mixrgb_node.inputs["Color1"])
    link=links.new(multiply_node.outputs["Value"],output_node.inputs["Displacement"])
    link=links.new(add_node.outputs["Value"],multiply_node.inputs[0])
    link=links.new(voronoi_node.outputs["Fac"],add_node.inputs[0])
    link=links.new(multiply_node_two.outputs["Value"],add_node.inputs[1])
    link=links.new(voronoi_node_two.outputs["Fac"],multiply_node_two.inputs[0])
    link=links.new(texture_coordinate_node.outputs["Object"],voronoi_node.inputs["Vector"])
    link=links.new(texture_coordinate_node.outputs["Object"],voronoi_node_two.inputs["Vector"])
    
def Stationary_Water_Shader_3(material):
    #Make the material use nodes
    material.use_nodes=True
    #Set the variable node_tree to be the material's node tree and variable nodes to be the node tree's nodes
    node_tree=material.node_tree
    nodes=material.node_tree.nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #Create the output node
    output_node=nodes.new('ShaderNodeOutputMaterial')
    output_node.location=(300,300)
    #Create the first mix shader node
    mix_node=nodes.new('ShaderNodeMixShader')
    mix_node.location=(0,300)
    mix_node.inputs["Fac"].default_value=(0.8)
    #Create the transparent shader node
    transparent_node=nodes.new('ShaderNodeBsdfTransparent')
    transparent_node.location=(-300,600)
    #Create the second mix shader node
    mix_node_two=nodes.new('ShaderNodeMixShader')
    mix_node_two.location=(-300,300)
    mix_node.inputs["Fac"].default_value=(0.8)
    #Create the refraction shader node
    refraction_node=nodes.new('ShaderNodeBsdfRefraction')
    refraction_node.location=(-600,300)
    refraction_node.inputs["Roughness"].default_value=(0.5)
    refraction_node.inputs["IOR"].default_value=(1.333333)
    #Create the glossy shader node
    glossy_node=nodes.new('ShaderNodeBsdfGlossy')
    glossy_node.location=(-600,0)
    glossy_node.inputs["Roughness"].default_value=(0.5)
    #Create the rgb mix shader
    rgbmix_node=nodes.new('ShaderNodeMixRGB')
    rgbmix_node.location=(-900,300)
    rgbmix_node.inputs["Fac"].default_value=(0.9)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-1200,300)
    rgba_node.label = "RGBA"
    #Create the wave texture node
    wave_node=nodes.new('ShaderNodeTexWave')
    wave_node.location=(-1200,0)
    wave_node.inputs["Scale"].default_value=(1)
    wave_node.inputs["Distortion"].default_value=(5)
    wave_node.inputs["Detail"].default_value=(5)
    wave_node.inputs["Detail Scale"].default_value=(5)
    #Create the multiply node
    multiply_node=nodes.new('ShaderNodeMath')
    multiply_node.location=(-600,-300)
    multiply_node.operation=('MULTIPLY')
    multiply_node.inputs[1].default_value=(100)
    #Link the nodes
    links=node_tree.links
    link=links.new(mix_node.outputs["Shader"],output_node.inputs["Surface"])
    link=links.new(transparent_node.outputs["BSDF"],mix_node.inputs[1])
    link=links.new(mix_node_two.outputs["Shader"],mix_node.inputs[2])
    link=links.new(refraction_node.outputs["BSDF"],mix_node_two.inputs[1])
    link=links.new(glossy_node.outputs["BSDF"],mix_node_two.inputs[2])
    link=links.new(rgbmix_node.outputs["Color"],refraction_node.inputs["Color"])
    link=links.new(rgbmix_node.outputs["Color"],glossy_node.inputs["Color"])
    link=links.new(rgba_node.outputs["Color"],rgbmix_node.inputs["Color1"])
    link=links.new(wave_node.outputs["Color"],rgbmix_node.inputs["Color2"])
    link=links.new(multiply_node.outputs["Value"],output_node.inputs["Displacement"])
    link=links.new(wave_node.outputs["Fac"],multiply_node.inputs[0])
    
def Flowing_Water_Shader(material):
    material.use_nodes=True
    
def Slime_Shader(material):
    #Make the material use nodes
    material.use_nodes=True
    #Set the variable node_tree to be the material's node tree and variable nodes to be the node tree's nodes
    node_tree=material.node_tree
    nodes=material.node_tree.nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #commented out alt versions
    #node_tree=bpy.data.materials[material].node_tree
    #diffuse_node=nodes.get('ShaderNodeBsdfDiffuse')
    #Create the output node
    output_node=nodes.new('ShaderNodeOutputMaterial')
    output_node.location=(300,300)
    #Create the mix shader
    mix_node=nodes.new('ShaderNodeMixShader')
    mix_node.location=(0,300)
    #Create the diffuse node
    diffuse_node=nodes.new('ShaderNodeBsdfDiffuse')
    diffuse_node.location=(-300,300)
    #Create the transparent node
    transparent_node=nodes.new('ShaderNodeBsdfTransparent')
    transparent_node.location=(-300,0)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-600,300)
    rgba_node.label = "RGBA"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],diffuse_node.inputs["Color"])
    link=links.new(transparent_node.outputs["BSDF"],mix_node.inputs[1])
    link=links.new(diffuse_node.outputs["BSDF"],mix_node.inputs[2])
    link=links.new(mix_node.outputs["Shader"],output_node.inputs["Surface"])

def Ice_Shader(material):
    #Make the material use nodes
    material.use_nodes=True
    #Set the variable node_tree to be the material's node tree and variable nodes to be the node tree's nodes
    node_tree=material.node_tree
    nodes=material.node_tree.nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #commented out alt versions
    #node_tree=bpy.data.materials[material].node_tree
    #diffuse_node=nodes.get('ShaderNodeBsdfDiffuse')
    #Create the output node
    output_node=nodes.new('ShaderNodeOutputMaterial')
    output_node.location=(300,300)
    #Create the mix shader
    mix_node=nodes.new('ShaderNodeMixShader')
    mix_node.location=(0,300)
    #Create the diffuse node
    diffuse_node=nodes.new('ShaderNodeBsdfDiffuse')
    diffuse_node.location=(-300,300)
    #Create the transparent node
    transparent_node=nodes.new('ShaderNodeBsdfTransparent')
    transparent_node.location=(-300,0)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-600,300)
    rgba_node.label = "RGBA"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],diffuse_node.inputs["Color"])
    link=links.new(transparent_node.outputs["BSDF"],mix_node.inputs[1])
    link=links.new(diffuse_node.outputs["BSDF"],mix_node.inputs[2])
    link=links.new(mix_node.outputs["Shader"],output_node.inputs["Surface"])
    
def Stained_Glass_Shader(material):
    material.use_nodes=True
    
def Sky_Day_Shader(world):
    #Make the world use nodes
    world.use_nodes=True
    #Set the variable node_tree to be the world's node tree and variable nodes to be the node tree's nodes
    node_tree=world.node_tree
    nodes=world.node_tree.nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #Add the output node
    output_node=nodes.new('ShaderNodeOutputWorld')
    output_node.location=(300,300)
    #Add the background node
    background_node=nodes.new('ShaderNodeBackground')
    background_node.location=(0,300)
    #Add the sky texture node
    sky_node=nodes.new('ShaderNodeTexSky')
    sky_node.location=(-300,300)
    #Link the nodes
    links=node_tree.links
    link=links.new(background_node.outputs["Background"],output_node.inputs["Surface"])
    link=links.new(sky_node.outputs["Color"],background_node.inputs["Color"])
    

def Sky_Night_Shader(world):
    #Make the world use nodes
    world.use_nodes=True
    #Set the variable node_tree to be the world's node tree and variable nodes to be the node tree's nodes
    node_tree=world.node_tree
    nodes=world.node_tree.nodes
    for eachNode in nodes:
        nodes.remove(eachNode)
    #Add the output node
    output_node=nodes.new('ShaderNodeOutputWorld')
    output_node.location=(300,300)
    #Add the mix node
    mix_node=nodes.new('ShaderNodeMixShader')
    mix_node.location=(0,300)
    mix_node.inputs["Fac"].default_value=(0.005)
    #Add the first background node
    background_node=nodes.new('ShaderNodeBackground')
    background_node.location=(-300,300)
    #Add the second background node
    background_node_two=nodes.new('ShaderNodeBackground')
    background_node_two.location=(-300,0)
    #Add the sky texture node
    sky_node=nodes.new('ShaderNodeTexSky')
    sky_node.location=(-600,0)
    #Add the colorramp node
    colorramp_node=nodes.new('ShaderNodeValToRGB')
    colorramp_node.location=(-600,300)
    colorramp_node.color_ramp.interpolation=('CONSTANT')
    colorramp_node.color_ramp.elements[1].position=(0.04)
    colorramp_node.color_ramp.elements[1].color=(0,0,0,255)
    colorramp_node.color_ramp.elements[0].color=(255,255,255,255)
    #Add the voronoi texture
    voronoi_node=nodes.new('ShaderNodeTexVoronoi')
    voronoi_node.location=(-900,300)
    voronoi_node.coloring=("CELLS")
    voronoi_node.inputs["Scale"].default_value=(1000)
    #Add the combine node
    combine_node=nodes.new('ShaderNodeCombineXYZ')
    combine_node.location=(-1200,300)
    #Add the first add node
    add_node=nodes.new('ShaderNodeMath')
    add_node.location=(-1500,300)
    add_node.operation=('ADD')
    add_node.inputs[1].default_value=(300)
    #Add the second add node
    add_node_two=nodes.new('ShaderNodeMath')
    add_node_two.location=(-1500,0)
    add_node_two.operation=('ADD')
    add_node_two.inputs[1].default_value
    #Add the first round node
    round_node=nodes.new('ShaderNodeMath')
    round_node.location=(-1800,300)
    round_node.operation=('ROUND')
    round_node.inputs[1].default_value=(0.5)
    #Add the second  round node
    round_node_two=nodes.new('ShaderNodeMath')
    round_node_two.location=(-1800,0)
    round_node_two.operation=('ROUND')
    round_node_two.inputs[1].default_value=(0.5)
    #Add the first multiply node
    multiply_node=nodes.new('ShaderNodeMath')
    multiply_node.location=(-2100,300)
    multiply_node.operation=('MULTIPLY')
    multiply_node.inputs[1].default_value=(300)
    #Add the second multiply node
    multiply_node_two=nodes.new('ShaderNodeMath')
    multiply_node_two.location=(-2100,0)
    multiply_node_two.operation=('MULTIPLY')
    multiply_node_two.inputs[1].default_value=(300)
    #Add the separate node
    separate_node=nodes.new('ShaderNodeSeparateXYZ')
    separate_node.location=(-2400,300)
    #Add the texture coordinate node
    texture_coordinate_node=nodes.new('ShaderNodeTexCoord')
    texture_coordinate_node.location=(-2700,300)
    #Link the nodes
    links=node_tree.links
    link=links.new(mix_node.outputs["Shader"],output_node.inputs["Surface"])
    link=links.new(background_node.outputs["Background"],mix_node.inputs[1])
    link=links.new(background_node_two.outputs["Background"],mix_node.inputs[2])
    link=links.new(colorramp_node.outputs["Color"],background_node.inputs["Color"])
    link=links.new(sky_node.outputs["Color"],background_node_two.inputs["Color"])
    link=links.new(voronoi_node.outputs["Color"],colorramp_node.inputs["Fac"])
    link=links.new(combine_node.outputs["Vector"],voronoi_node.inputs["Vector"])
    link=links.new(add_node.outputs["Value"],combine_node.inputs["X"])
    link=links.new(add_node_two.outputs["Value"],combine_node.inputs["Y"])
    link=links.new(round_node.outputs["Value"],add_node.inputs[0])
    link=links.new(round_node_two.outputs["Value"],add_node_two.inputs[0])
    link=links.new(multiply_node.outputs["Value"],round_node.inputs[0])
    link=links.new(multiply_node_two.outputs["Value"],round_node_two.inputs[0])
    link=links.new(separate_node.outputs["X"],multiply_node.inputs[0])
    link=links.new(separate_node.outputs["Y"],multiply_node_two.inputs[0])
    link=links.new(texture_coordinate_node.outputs["Camera"],separate_node.inputs["Vector"])
   

def Sun_Shader():
    pass


#MAIN

def main():
      
    #packing all the files into one .blend 
    bpy.ops.file.pack_all()
    print("Files packed")
    
    
    #Setting the render engine to Cycles
    if USER_INPUT_SCENE==True:
        #get the scene to use
        scene=input("Please enter the scene you would like to use (default \"Scene\"): ")
        if scene=="":
            scene = "Scene"
        #Set the render engine to Cycles
        bpy.data.scenes[scene].render.engine='CYCLES'
    else:
        for scene in bpy.data.scenes:
            scene.render.engine = 'CYCLES'
    print("Render engine set to Cycles")
            
    
    try:
        bpy.data.images[PREFIX+"-RGBA.png"]
    except:
        print("ERROR! PREFIX INVALID")
    
    
    #for every material
    for material in bpy.data.materials:
        #print that the material is now being worked on
        print("Started  "+str(material.name))
        #if the material is transparent use a special shader
        if any(material==bpy.data.materials.get(transparentBlock) for transparentBlock in transparentBlocks):
            Transparent_Shader(material)
        #if the material is a light emmitting block use a special shader
        elif any(material==bpy.data.materials.get(lightBlock) for lightBlock in lightBlocks):
            Light_Emiting_Shader(material)
        #if the material is a light emmitting transparent block use a special shader
        elif any(material==bpy.data.materials.get(lightTransparentBlocks) for lightTransparentBlocks in lightTransparentBlocks):
            Transparent_Emiting_Shader(material)
        #if the material is stationary water, use a special shader
        elif material==bpy.data.materials.get("Stationary_Water"):
            if WATER_SHADER_TYPE==0:
                Normal_Shader(material)
            elif WATER_SHADER_TYPE==1:
                Stationary_Water_Shader_1(material)
            elif WATER_SHADER_TYPE==2:
                Stationary_Water_Shader_2(material)
            elif WATER_SHADER_TYPE==3:
                Stationary_Water_Shader_3(material)
            else:
                print("ERROR! COULD NOT SET UP WATER")
        #if the material is flowing water, use a special shader
        elif material==bpy.data.materials.get("Flowing_Water"):
            pass
        #if the material is slime, use a special shader
        elif material==bpy.data.materials.get("Slime"):
            Slime_Shader(material)
        #if the material is ice, use a special shader
        elif material==bpy.data.materials.get("Ice"):
            Ice_Shader(material)
        #else use a normal shader
        else:
            Normal_Shader(material)
        #print the material has finished
        print("Finished "+str(material.name))
        
    #Set up the sky
    print("Started shading sky")
    for worlds in bpy.data.worlds:
        world=worlds
        if TIME_OF_DAY>=6.5 and TIME_OF_DAY<=19.5:
            Sky_Day_Shader(world)
        elif TIME_OF_DAY<6.5 or TIME_OF_DAY>19.5:
            Sky_Night_Shader(world)
        else:
            print("ERROR, FAILED TO SET UP SKY")
    print("Sky shaded")
    
    
    #Set up the sun
    print("Started shading sun")
    Sun_Shader()
    print("Sun shaded")



print("Started")
    
    
#importing the Blender Python library
import bpy
print("Libraries imported")        
main()