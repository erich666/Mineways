# Cycles Mineways setup
# Version 1.2.2, 4/10/16
# Copyright © 2016
# Please send suggestions or report bugs at https://github.com/JMY1000/CyclesMineways/
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
PREFIX = ""
#If this list has scenes, the script only use those scenes to work with;
#otherwise, it will use all scenes
#example: USER_INPUT_SCENE = ["scene","scene2","randomScene123"]
USER_INPUT_SCENE = []
#Cloud state, either True or False
CLOUD_STATE = False
#Changing the number here changes what water shader will be used, 0 to use the normal shader, 1 to use a partially transparent but still only textured shader, 2 for a choppy shader, 3 for a wavy shader
WATER_SHADER_TYPE = 1
#Changing the number here changes the type of sky shader used, 0 for no shader
SKY_SHADER_TYPE = 0
#Time of day–note that the decimal is not in minutes, and is a fraction (ex. 12:30 is 12.50)
TIME_OF_DAY = 12.00
#Decide if  lava is animated
LAVA_ANIMATION = False
#Use virtual displacement (changes normals for illusion of roughness) for wooden plank blocks
#NOTE: this currently only works for oak wood planks
DISPLACE_WOOD = False

#List of transparent blocks
transparentBlocks = ["Acacia_Leaves","Dark_Oak_Leaves","Acacia_Door","Activator_Rail","Bed","Birch_Door","Brewing_Stand","Cactus","Carrot","Carrots","Cauldron","Chorus_Flower","Chorus_Flower_Dead","Chorus_Plant","Cobweb",
    "Cocoa","Crops","Dandelion","Dark_Oak_Door","Dead_Bush","Detector_Rail","Enchantment_Table","Glass","Glass_Pane","Grass","Iron_Bars","Iron_Door","Iron_Trapdoor","Jack_o'Lantern","Jungle_Door","Large_Flower",
    "Leaves","Lily_Pad","Melon_Stem","Monster_Spawner","Nether_Portal","Nether_Wart","Oak_Leaves","Oak_Sapling","Poppy","Potato","Potatoes","Powered_Rail","Powered_Rail_(off)","Pumpkin_Stem","Rail","Red_Mushroom",
    "Redstone_Comparator_(off)","Redstone_Torch_(off)","Repeater_(off)","Sapling","Spruce_Door","Stained_Glass","Sugar_Cane","Sunflower","Tall_Grass","Trapdoor","Vines","Wheat","Wooden_Door"]
#List of light emitting blocks
lightBlocks = ["End_Portal","Ender_Chest","Flowing_Lava","Glowstone","Redstone_Lamp_(on)","Stationary_Lava","Sea_Lantern"]
#List of light emitting and transparent block
lightTransparentBlocks = ["Beacon","Brown_Mushroom","Dragon_Egg","Endframe","End_Rod","Fire","Powered_Rail_(on)","Redstone_Comparator_(on)","Redstone_Torch_(on)","Repeater_(on)","Torch"]


#SHADERS

def Normal_Shader(material,rgba_image):
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
    diffuse_node.inputs[1].default_value=0.3 # sets diffuse to 0.3 for all normal blocks
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = rgba_image
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
    diffuse_node.location=(-300,400)
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
    #alpha_node=nodes.new('ShaderNodeTexImage')
    #alpha_node.image = bpy.data.images[PREFIX+"-Alpha.png"]
    #alpha_node.interpolation=('Closest')
    #alpha_node.location=(-600,0)
    #alpha_node.label = "Alpha"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],diffuse_node.inputs["Color"])
    link=links.new(rgba_node.outputs["Alpha"],mix_node.inputs["Fac"])
    #link=links.new(alpha_node.outputs["Color"],mix_node.inputs["Fac"])
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
    output_node.location=(600,300)
    #Create the diffuse deciding mix node
    diffuse_mix_node=nodes.new('ShaderNodeMixShader')
    diffuse_mix_node.location=(300,300)
    #Create the Light Path Node
    light_path_node=nodes.new('ShaderNodeLightPath')
    light_path_node.location=(0,600)
    #Create the diffuse emission
    indirect_emission_node=nodes.new('ShaderNodeEmission')
    indirect_emission_node.location=(0,100)
    #Create the Light Falloff node for indirect emission
    light_falloff_node=nodes.new('ShaderNodeLightFalloff')
    light_falloff_node.location=(-300,0)
    light_falloff_node.inputs[0].default_value=5000 #sets strength of light
    light_falloff_node.inputs[1].default_value=0.05 #sets smooth level of light
    #Create the HSV node to brighten the light
    hsv_node=nodes.new('ShaderNodeHueSaturation')
    hsv_node.location=(-300,200)
    hsv_node.inputs["Value"].default_value=3 # brightens the color for better lighting
    #Create the direct emission node
    direct_emission_node=nodes.new('ShaderNodeEmission')
    direct_emission_node.location=(0,300)
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = bpy.data.images[PREFIX+"-RGBA.png"]
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-600,300)
    rgba_node.label = "RGBA"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],direct_emission_node.inputs["Color"])
    link=links.new(rgba_node.outputs["Color"],hsv_node.inputs["Color"])
    link=links.new(hsv_node.outputs["Color"],indirect_emission_node.inputs["Color"])
    link=links.new(light_falloff_node.outputs[1],indirect_emission_node.inputs[1]) #connects linear output to emission strength
    link=links.new(indirect_emission_node.outputs["Emission"],diffuse_mix_node.inputs[2])
    link=links.new(direct_emission_node.outputs["Emission"],diffuse_mix_node.inputs[1])
    link=links.new(light_path_node.outputs[2],diffuse_mix_node.inputs["Fac"]) #links "is diffuse ray" to factor of mix node
    link=links.new(diffuse_mix_node.outputs["Shader"],output_node.inputs["Surface"])
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
    emission_node.location=(-300,400)
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
    #alpha_node=nodes.new('ShaderNodeTexImage')
    #alpha_node.image = bpy.data.images[PREFIX+"-Alpha.png"]
    #alpha_node.interpolation=('Closest')
    #alpha_node.location=(-600,0)
    #alpha_node.label = "Alpha"
    #Link the nodes
    links=node_tree.links
    link=links.new(rgba_node.outputs["Color"],emission_node.inputs["Color"])
    link=links.new(rgba_node.outputs["Alpha"],mix_node.inputs["Fac"])
    #link=links.new(alpha_node.outputs["Color"],mix_node.inputs["Fac"])
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
    #Add the color correct node
    HSV_node=nodes.new('ShaderNodeHueSaturation')
    HSV_node.inputs["Value"].default_value=1.6 #Corrects the color value to be the same as Minecraft's sky
    HSV_node.location=(-300,300)
    #Add the sky texture node
    sky_node=nodes.new('ShaderNodeTexSky')
    sky_node.location=(-600,300)
    #Link the nodes
    links=node_tree.links
    link=links.new(background_node.outputs["Background"],output_node.inputs["Surface"])
    link=links.new(sky_node.outputs["Color"],HSV_node.inputs["Color"])
    link=links.new(HSV_node.outputs["Color"],background_node.inputs["Color"])
    

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
    output_node.location=(600,300)
    #Add solid color background for diffuse textures
    solid_background_node=nodes.new('ShaderNodeBackground')
    solid_background_node.location=(0,150)
    solid_background_node.inputs["Color"].default_value=(0.1,0.1,0.1,1)
    #Add Light Path Node to make sure solid colour is only used for diffuse shaders
    light_path_node=nodes.new('ShaderNodeLightPath')
    light_path_node.location=(0,600)
    #Add mix shader to add the diffuse-only background
    diffuse_mixer_node=nodes.new('ShaderNodeMixShader')
    diffuse_mixer_node.location=(300,300)
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
    link=links.new(diffuse_mixer_node.outputs["Shader"],output_node.inputs["Surface"])
    link=links.new(mix_node.outputs["Shader"],diffuse_mixer_node.inputs[1])
    link=links.new(solid_background_node.outputs["Background"],diffuse_mixer_node.inputs[2])
    link=links.new(light_path_node.outputs[2],diffuse_mixer_node.inputs[0]) # connects "Is Diffuse Ray" to factor
    #link=links.new(mix_node.outputs["Shader"],output_node.inputs["Surface"])
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


def Wood_Displacement_Texture(material,rgba_image):
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
    diffuse_node.inputs[1].default_value=0.3 # sets diffuse to 0.3
    #Create the rgba node
    rgba_node=nodes.new('ShaderNodeTexImage')
    rgba_node.image = rgba_image
    rgba_node.interpolation=('Closest')
    rgba_node.location=(-300,300)
    rgba_node.label = "RGBA"
    
    #Create displacement node tree
    
    #Create magic node 1
    magic_node_one=nodes.new('ShaderNodeTexMagic')
    magic_node_one.location=(-900,200)
    magic_node_one.turbulence_depth=6 #sets depth to 6
    magic_node_one.inputs[1].default_value=5 #sets scale to 5
    magic_node_one.inputs[2].default_value=10 #sets distortion to 10
    
    #Create magic node 2
    magic_node_two=nodes.new('ShaderNodeTexMagic')
    magic_node_two.location=(-900,0)
    magic_node_two.turbulence_depth=5 #sets depth to 5
    magic_node_two.inputs[1].default_value=3.3 #sets scale to 3.3
    magic_node_two.inputs[2].default_value=2.7 #sets distortion to 2.7
    
    #Create Add node
    #Connects to magic node 1 and 2
    math_add_node_one=nodes.new('ShaderNodeMath')
    math_add_node_one.location=(-600,0)
    math_add_node_one.operation="ADD"
    
    #Create noise texture
    noise_node=nodes.new('ShaderNodeTexNoise')
    noise_node.location=(-900,-200)
    noise_node.inputs[1].default_value=6.9 #sets scale to 6.9
    noise_node.inputs[2].default_value=5 #set detail to 5
    noise_node.inputs[3].default_value=8 #sets distortion to 8
    
    #Create multiply
    #Connects to noise and 5
    math_multiply_node=nodes.new('ShaderNodeMath')
    math_multiply_node.location=(-600,-200)
    math_multiply_node.operation="MULTIPLY"
    math_multiply_node.inputs[1].default_value=5 #sets multiply value to 5
    
    #Create 2nd Add node
    #Connects to Add node and multiply node
    math_add_node_two=nodes.new('ShaderNodeMath')
    math_add_node_two.operation="ADD"
    math_add_node_two.location=(-300,0)
    
    
    #Create Divide node
    #Connect from 2nd add node and input [1] to 10
    #Connects to materials output
    math_divide_node=nodes.new('ShaderNodeMath')
    math_divide_node.location=(0,150)
    math_divide_node.operation="DIVIDE"
    math_divide_node.inputs[1].default_value=10
    
    #Link the nodes
    links=node_tree.links
    #link surface modifiers
    link=links.new(rgba_node.outputs["Color"],diffuse_node.inputs["Color"])
    link=links.new(diffuse_node.outputs["BSDF"],output_node.inputs["Surface"])
    #link displacement modifiers
    link=links.new(magic_node_one.outputs["Fac"],math_add_node_one.inputs[0])
    link=links.new(magic_node_two.outputs["Fac"],math_add_node_one.inputs[1])
    link=links.new(math_add_node_one.outputs[0],math_add_node_two.inputs[0])
    link=links.new(noise_node.outputs["Fac"],math_multiply_node.inputs[0])
    link=links.new(math_multiply_node.outputs[0],math_add_node_two.inputs[1])
    link=links.new(math_add_node_two.outputs[0],math_divide_node.inputs[0])
    link=links.new(math_divide_node.outputs[0],output_node.inputs["Displacement"])


#MAIN

def main():
    
    print("Main started")

    #packing all the files into one .blend 
    print("Packing files")
    bpy.ops.file.pack_all()
    print("Files packed")
    
    
    #Setting the render engine to Cycles
    if len(USER_INPUT_SCENE)==0:
        for scene in bpy.data.scenes:
            scene.render.engine = 'CYCLES'
    else:
        for w in USER_INPUT_SCENE:
            #Set the render engine to Cycles
            bpy.data.scenes[w].render.engine='CYCLES'
    print("Render engine set to Cycles for selected scenes")
            
    
    try:
        texture_rgba_image = bpy.data.images[PREFIX+"-RGBA.png"]
    except:
        print("Cannot find image. PREFIX is invalid.")
        return
    
    print("Setting up textures")
    #for every material
    for material in bpy.data.materials:
        if material.active_texture:
            if len(material.active_texture.name)>=2:
                if (material.active_texture.name[0:2]=="Kd"):
                    material_suffix = material.name[material.name.rfind("."):len(material.name)] # gets the .001 .002 .003 ... of the material
                    try:
                        int(material_suffix[1:])
                    except:
                        material_suffix=""
                    #if the material is transparent use a special shader
                    if any(material==bpy.data.materials.get(transparentBlock+material_suffix) for transparentBlock in transparentBlocks):
                        print(material.name+" is transparent.")
                        Transparent_Shader(material)
                    #if the material is a light emmitting block use a special shader
                    elif any(material==bpy.data.materials.get(lightBlock+material_suffix) for lightBlock in lightBlocks):
                        print(material.name+" is light block.")
                        Light_Emiting_Shader(material)
                    #if the material is a light emmitting transparent block use a special shader
                    elif any(material==bpy.data.materials.get(lightTransparentBlocks+material_suffix) for lightTransparentBlocks in lightTransparentBlocks):
                        print(material.name+" is transparent light block.")
                        Transparent_Emiting_Shader(material)
                    #if the material is stationary water, use a special shader
                    elif material==bpy.data.materials.get("Stationary_Water"+material_suffix):
                        print(material.name+" is water.")
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
                    elif material==bpy.data.materials.get("Flowing_Water"+material_suffix):
                        print(material.name+" is flowing water.")
                        pass
                    #if the material is slime, use a special shader
                    elif material==bpy.data.materials.get("Slime"+material_suffix):
                        print(material.name+" is slime.")
                        Slime_Shader(material)
                    #if the material is ice, use a special shader
                    elif material==bpy.data.materials.get("Ice"+material_suffix):
                        print(material.name+" is ice.")
                        Ice_Shader(material)
                    #if the material is wood and DISPLACE_WOOD is True
                    elif (material==bpy.data.materials.get("Oak_Wood_Planks"+material_suffix))and(DISPLACE_WOOD):
                        print(material.name+" is displaced wooden planks.")
                        Wood_Displacement_Texture(material,texture_rgba_image)
                    #else use a normal shader
                    else:
                        print(material.name+" is normal.")
                        Normal_Shader(material,texture_rgba_image)
    print("Finished setting up materials")
        
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
    
    #Remove unnecessary textures
    print("Removing unnecessary textures")
    for img in bpy.data.images:
        try:
            suffix = img.name.rfind(".")
            int(img.name[suffix+1:])
            print("Texture "+img.name+" removed for being a duplicate.")
            img.user_clear()
            bpy.data.images.remove(img)
        except:
            if (img.name==PREFIX+"-Alpha.png") or (img.name==PREFIX+"-RGB.png"):
                print("Texture "+img.name+" removed for being redundant")
                img.user_clear()
                bpy.data.images.remove(img)
            else:
                print("Texture "+img.name+" was not removed.")
    print("Finished removing unnecessary textures")



print("\nStarted Cycles Mineways import script.\n")

#importing the Blender Python library
import bpy
print("Libraries imported")

main()

print("\nCycles Mineways has finished.\n")
