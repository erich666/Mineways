#Cycles Mineways setup
#Created by Jonathan Edelman
#Version 1.0.1, 9/2/15
#This software is distributed under a GNU GPL license; you are free to do most whatever you want.
#Please send suggestions or report bugs at jonathanedelman@mail.com

# Distributed with Mineways, http://mineways.com

# How to use:
# First, you must edit this script a tiny bit. You need to change the PREFIX value listed in the
# CONSTANTS section. Change "sampler" to whatever your save file name is. For example, if it's
# castle.obj, then do this:
# PREFIX="castle"

# To use the script within Blender, for use with the Cycles renderer:

# To the right of the "Help" menu in the upper left, click on the "keys" icon next to the word
#"Default" and pick "Scripting".

# At the bottom of the gray window you'll see a menu "Text"; click it and select "Open Text
# Block". Go to the directory where this file "cycles_mineways.py" is and select it. You should now
# see some text in the gray window.

# To apply this script, click on the "Run Script" button at the bottom right of the text window.

# To see that the script did something, from the upper left select "Window" and "Toggle System
# Console". If you don't have a "Window" menu item between "Render" and "Help", you have an
# older version of Blender and should consider updating. This menu item pops up a console window.
# It isn't critical to see this window, but gives you a warm and fuzzy feeling that the script has worked.


#CONSTANTS


#The prefix of the texture files it uses
PREFIX="sampler"
#If this is true, the script will request a scene to work with; otherwise, it will use all scenes
USER_INPUT_SCENE=False
#Cloud state, either True or False
CLOUD_STATE=False
#Changing the number here changes what water shader will be used, 0 to use the normal shader
WATER_SHADER_TYPE=int(0)
#Changine the number here changes the type of sky shader used, 0 for no shader
SKY_SHADER_TYPE=int(0)
#Time of day
TIME_OF_DAY=float(12.00)

#List of transparent blocks
transparentBlocks=["Acacia/Dark_Oak_Leaves","Activator_Rail","Bed","Brewing_Stand","Brown_Mushroom","Cactus","Carrots","Cauldron","Cobweb","Crops","Dandelion","Dead_Bush","Detector_Rail","Enchantment_Table","Glass","Glass_Pane","Grass","Iron_Bars","Iron_Door","Iron_Trapdoor","Large_Flower","Leaves","Lilly_Pad","Melon_Stem","Monster_Spawner","Nether_Wart","Poppy","Potatoes","Pumpkin_Stem","Rail","Red_Mushroom","Redstone_Comparator_(off)","Redstone_Torch_(off)","Repeater_(off)","Sapling","Sign_Post","Sugar_Cane","Tall_Grass","Trapdoor","Vines","Wall_Sign","Wooden_Door"]
#List of light emitting blocks
lightBlocks=["End_Portal","Redstone_Lamp_(on)","Glowstone","Stationary_Lava"]
#List of light emitting and transparent block
lightTransparentBlocks=["Fire","Redstone_Comparator_(on)","Redstone_Torch_(on)","Repeater_(on)","Torch"]


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

def Stationary_Water_Shader(material):
    material.use_nodes=True
    
def Flowing_Water_Shader(material):
    material.use_nodes=True
    
def Slime_Shader(material):
    material.use_nodes=True

def Ice_Shader(material):
    material.use_nodes=True
    
def Stained_Glass_Shader(material):
    material.use_nodes=True
    
def Sky_Shader():
    pass

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
            
    
    #for every material
    for material in bpy.data.materials:
        #print that the material is now being worked on
        print("Started  "+str(material))
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
            pass
        #if the material is flowing water, use a special shader
        elif material==bpy.data.materials.get("Flowing_Water"):
            pass
        #if the material is slime, use a special shader
        elif material==bpy.data.materials.get("Slime"):
            pass
        #if the material is ice, use a special shader
        elif material==bpy.data.materials.get("Ice"):
            pass
        #if the material is stained glass, use a special shader
        elif material==bpy.data.materials.get("Stained_Glass") or material==bpy.data.materials.get("Stained_Glass_Pane"):
            pass
        #else use a normal shader
        else:
            Normal_Shader(material)
        #print the material has finished
        print("Finished "+str(material))
        
    #Set up the sky
    print("Started shading sky")
    Sky_Shader()
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