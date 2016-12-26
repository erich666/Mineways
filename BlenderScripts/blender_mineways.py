# Mineways Blender script, for quickly setting materials for Mineways models imported into Blender
# Version 1.01, 6/22/2014
# Distributed with Mineways, http://mineways.com
# Original script by Nicky, see https://www.youtube.com/watch?v=X41QQqCib3A
# Heavily modified by Wyatt Jameson, see https://www.youtube.com/watch?v=hb906Xzh7dQ
# Added "use_mipmap = False" to avoid edge artifacts for grass blocks, etc.

# How to use:
# To the right of the "Help" menu in the upper left, click on the "keys" icon next to the word
#"Default" and pick "Scripting".

# At the bottom of the gray window you'll see a menu "Text"; click it and select "Open Text
# Block". Go to the directory where "blender_mineways.py" is and select it. You should now
# see some text in the gray window.

# To apply this script, click on the "Run Script" button at the bottom right of the text window.

# To see that the script did something, from the upper left select "Window" and "Toggle System
# Console". If you don't have a "Window" menu item between "Render" and "Help", you have an
# older version of Blender and should consider updating. This menu item pops up a console window.
# It isn't critical to see this window, but gives you a warm and fuzzy feeling that the script has worked.

print ("Started mineways material, texture, and world setup...")
import bpy
print ("Imported bpy")

print ("Setting up materials...")
for material in bpy.data.materials:
	bpy.data.materials[material.name].use_textures = [True, False, False, False, False, False, False, False, False, False, False, False, False, False, False, False, False, False]
	print ("Disabled texture slot for material: "+material.name)
	bpy.data.materials[material.name].texture_slots[0].use_map_alpha = True
	print ("Enabled use of alpha map for material: "+material.name)
	bpy.data.materials[material.name].use_transparent_shadows = True
	print ("Enabled receive transparent for material: "+material.name)
	if (material.name == 'Water') or (material.name == 'Stationary_Water'):
		bpy.data.materials[material.name].raytrace_transparency.fresnel = 2
		bpy.data.materials[material.name].transparency_method = 'RAYTRACE'
		bpy.data.materials[material.name].raytrace_transparency.ior = 1.333
		
		bpy.data.materials[material.name].texture_slots[0].diffuse_color_factor = 0.8
		bpy.data.materials[material.name].texture_slots[0].use_map_mirror = True
		bpy.data.materials[material.name].texture_slots[0].mirror_factor = 0.3
		bpy.data.materials[material.name].texture_slots[0].use_map_normal = True
		bpy.data.materials[material.name].texture_slots[0].normal_factor = 0.005
		bpy.data.materials[material.name].texture_slots[0].bump_method = 'BUMP_BEST_QUALITY'

		print ("Set up water for: "+material.name)



print ("Setting up textures...")
for texture in bpy.data.textures:
	if (texture.name.startswith("D")) or (texture.name.startswith("Kd")):
		bpy.data.textures[texture.name].use_interpolation = False
		# Added by Eric Haines, to avoid bleeding problems from edges for grass, etc.
		# You may want to comment this line out if you are doing animation.
		bpy.data.textures[texture.name].use_mipmap = False
		print ("Disabled interpolation for texture: "+texture.name)
		bpy.data.textures[texture.name].filter_type = 'BOX'
		print ("Set filter to BOX for texture: "+texture.name)
	else:
		print ("Jumped over texture: "+texture.name)

print ("Setting up world...")
bpy.context.scene.world.use_sky_paper = True
bpy.context.scene.world.use_sky_blend = True
bpy.context.scene.world.use_sky_real = True
bpy.context.scene.world.horizon_color = (0.460041, 0.703876, 1)
bpy.context.scene.world.zenith_color = (0.120707, 0.277449, 1)
print ("Set up the sky!")
bpy.context.scene.world.light_settings.use_ambient_occlusion = True
bpy.context.scene.world.light_settings.ao_factor = 0.35
print ("Set ambient occlusion (basically shadow intensity) to 0.35. Change this to something between 0.25 and 0.5 in World > Ambient Occlusion.")

		
print ("Program finished. Just add lighting and a camera, and you're ready to render!")
