/*
Copyright (c) 2010, Sean Kasun
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <gtk/gtk.h>
#include <stdint.h>
#include <string.h>
#include "MinewaysMap/blockInfo.h"

extern void selectColorScheme(GtkMenuItem *menuItem,gpointer user_data);

static void renameScheme(GtkCellRendererText *renderer,gchar *path,gchar *text,GtkTreeModel *model);
static void editScheme(GtkButton *widget,GtkListStore *store);
static void addScheme(GtkButton *widget,GtkListStore *store);
static void removeScheme(GtkButton *widget,GtkListStore *store);
static void updateColor(GtkCellRendererText *renderer,gchar *path,gchar *text,GtkTreeModel *model);
static void updateAlpha(GtkCellRendererText *renderer,gchar *path,gchar *text,GtkTreeModel *model);
static void renderId(GtkTreeViewColumn *column,GtkCellRenderer *cell,
	GtkTreeModel *model,GtkTreeIter *iter,gpointer data);
static void renderColor(GtkTreeViewColumn *column,GtkCellRenderer *cell,
	GtkTreeModel *model,GtkTreeIter *iter,gpointer data);

typedef struct
{
	int id;
	char name[256];
	uint32_t colors[256];
} ColorScheme;

GArray *schemes=NULL;

static void destroy()
{
}

GArray *menuitems=NULL;

#define COLORGROUP "ColorSchemes"

static GtkMenuShell *menu;
static int menupos;
static GSList *itemgroup;
void initColorSchemes(GtkMenuShell *menushell,int startpos,GSList *group)
{
	menu=menushell;
	menupos=startpos;
	itemgroup=group;

	menuitems=g_array_new(FALSE,TRUE,sizeof(GtkWidget *));
	schemes=g_array_new(FALSE,TRUE,sizeof(ColorScheme));

	GKeyFile *keyfile=g_key_file_new();
	gchar *filename=g_strdup_printf("%s/.Mineways",g_get_home_dir());
	if (g_key_file_load_from_file(keyfile,filename,G_KEY_FILE_NONE,NULL))
	{
		ColorScheme cs;
		int num=g_key_file_get_integer(keyfile,COLORGROUP,"count",NULL);
		for (int i=0;i<num;i++)
		{
			gchar *key=g_strdup_printf("name%d",i);
			gchar *key_name=g_key_file_get_string(keyfile,COLORGROUP,key,NULL);
            g_stpcpy(cs.name,key_name);
            g_free(key_name);
			g_free(key);
			key=g_strdup_printf("color%d",i);
			gsize len;
			gint *colors=g_key_file_get_integer_list(keyfile,COLORGROUP,key,&len,NULL);
			g_free(key);
			for (int j=0;j<len;j++)
				cs.colors[j]=colors[j];
            g_free(colors);
			cs.id=schemes->len;
			g_array_append_val(schemes,cs);
			GtkWidget *item=gtk_radio_menu_item_new_with_label(itemgroup,cs.name);
			itemgroup=gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(item));
			g_array_append_val(menuitems,item);
			gtk_menu_shell_insert(menu,item,menupos);
			g_signal_connect(G_OBJECT(item),"activate",
				G_CALLBACK(selectColorScheme),GINT_TO_POINTER(cs.id));
		}
	}
	g_key_file_free(keyfile);
	g_free(filename);
}
static void saveSchemes()
{
	GKeyFile *keyfile=g_key_file_new();
	gchar *filename=g_strdup_printf("%s/.Mineways",g_get_home_dir());
	g_key_file_load_from_file(keyfile,filename,G_KEY_FILE_NONE,NULL);

	//should delete all existing colorschemes
	g_key_file_remove_group(keyfile,COLORGROUP,NULL);

	ColorScheme *cs;
	int num=0;
	gint list[256];
	for (int i=0;i<schemes->len;i++)
	{
		cs=&g_array_index(schemes,ColorScheme,i);
		if (cs->id==-1) continue;
		gchar *key=g_strdup_printf("name%d",num);
		g_key_file_set_string(keyfile,COLORGROUP,key,cs->name);
		g_free(key);
		key=g_strdup_printf("color%d",num);
		for (int j=0;j<256;j++)
			list[j]=cs->colors[j];
		g_key_file_set_integer_list(keyfile,COLORGROUP,key,list,256);
		g_free(key);
		num++;
	}
	g_key_file_set_integer(keyfile,COLORGROUP,"count",num);

	gsize len;
	gchar *data=g_key_file_to_data(keyfile,&len,NULL);
	GFile *f=g_file_new_for_path(filename);
	g_file_replace_contents(f,data,len,NULL,FALSE,G_FILE_CREATE_NONE,NULL,NULL,NULL);
	g_free(data);
	g_free(filename);
	g_key_file_free(keyfile);
	g_object_unref(G_OBJECT(f));
}

ColorScheme *newScheme(char *name)
{
	ColorScheme cs;
	g_memmove(cs.name,name,256);
	cs.id=schemes->len;
	g_array_append_val(schemes,cs);
	GtkWidget *item=gtk_radio_menu_item_new_with_label(itemgroup,cs.name);
	itemgroup=gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(item));
	g_array_append_val(menuitems,item);
	gtk_menu_shell_insert(menu,item,menupos);
	g_signal_connect(G_OBJECT(item),"activate",
		G_CALLBACK(selectColorScheme),GINT_TO_POINTER(cs.id));
	gtk_widget_show(item);
	return &g_array_index(schemes,ColorScheme,cs.id);
}
static void initScheme(ColorScheme *cs)
{
	for (int i=0;i<numBlocks;i++)
	{
		uint32_t color=blocks[i].color;
		uint8_t r,g,b,a;
		r=color>>16;
		g=color>>8;
		b=color;
		double alpha=blocks[i].alpha;
		r/=alpha;
		g/=alpha;
		b/=alpha;
		a=alpha*255;
		color=(r<<24)|(g<<16)|(b<<8)|a;
		cs->colors[i]=color;
	}
	for (int i=numBlocks;i<256;i++)
		cs->colors[i]=0;
}

static GtkWidget *schemeList;
static GtkWidget *colorList;

void editColorSchemes(GtkMenuItem *menuItem,gpointer user_data)
{
	GtkWidget *win=gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win),"Color Schemes");
	gtk_window_set_default_size(GTK_WINDOW(win),300,300);
	g_signal_connect(G_OBJECT(win),"destroy",
		G_CALLBACK(destroy),NULL);

	//main hbox
	GtkWidget *hbox=gtk_hbox_new(FALSE,5);
	gtk_container_add(GTK_CONTAINER(win),hbox);

	GtkListStore *store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_INT);

	ColorScheme *cs;
	GtkTreeIter iter;
	for (int i=0;i<schemes->len;i++)
	{
		cs=&g_array_index(schemes,ColorScheme,i);
		if (cs->id==-1) continue;
		gtk_list_store_append(store,&iter);
		gtk_list_store_set(store,&iter,
			0,cs->name,1,cs->id,-1);
	}

	schemeList=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(schemeList));
	gtk_tree_selection_set_mode(sel,GTK_SELECTION_SINGLE);
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	renderer=gtk_cell_renderer_text_new();
	g_object_set(renderer,"editable",TRUE,NULL);
	g_signal_connect(G_OBJECT(renderer),"edited",
		G_CALLBACK(renameScheme),GTK_TREE_MODEL(store));
	column=gtk_tree_view_column_new_with_attributes(NULL,
		renderer,"text",0,NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(schemeList),column);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(schemeList),FALSE);
	gtk_box_pack_start(GTK_BOX(hbox),schemeList,TRUE,TRUE,0);

	//button box
	GtkWidget *vbox=gtk_vbox_new(FALSE,5);
	gtk_box_pack_start(GTK_BOX(hbox),vbox,FALSE,FALSE,0);

	GtkWidget *edit=gtk_button_new_with_label("Edit");
	gtk_box_pack_start(GTK_BOX(vbox),edit,FALSE,FALSE,0);
	g_signal_connect(G_OBJECT(edit),"clicked",
		G_CALLBACK(editScheme),store);

	GtkWidget *add=gtk_button_new_with_label("Add");
	gtk_box_pack_start(GTK_BOX(vbox),add,FALSE,FALSE,0);
	g_signal_connect(G_OBJECT(add),"clicked",
		G_CALLBACK(addScheme),store);
	
	GtkWidget *remove=gtk_button_new_with_label("Remove");
	gtk_box_pack_start(GTK_BOX(vbox),remove,FALSE,FALSE,0);
	g_signal_connect(G_OBJECT(remove),"clicked",
		G_CALLBACK(removeScheme),store);

	gtk_widget_show_all(win);
}
static void renameScheme(GtkCellRendererText *renderer,gchar *path,gchar *text,GtkTreeModel *model)
{
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(model,&iter,path);
	gtk_list_store_set(GTK_LIST_STORE(model),&iter,0,text,-1);
	int id;
	gtk_tree_model_get(model,&iter,1,&id,-1);
	ColorScheme *cs=&g_array_index(schemes,ColorScheme,id);
	g_stpcpy(cs->name,text);
	GtkWidget *item=g_array_index(menuitems,GtkWidget *,id);
	gtk_menu_item_set_label(GTK_MENU_ITEM(item),text);
	saveSchemes();
}
static void addScheme(GtkButton *widget,GtkListStore *store)
{
	ColorScheme *cs=newScheme("Color Scheme");
	initScheme(cs);

	GtkTreeIter iter;
	gtk_list_store_append(store,&iter);
	gtk_list_store_set(store,&iter,
		0,cs->name,1,cs->id,-1);
	saveSchemes();
}
static void removeScheme(GtkButton *widget,GtkListStore *store)
{
	GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(schemeList));
	GtkTreeIter iter;
	if (gtk_tree_selection_get_selected(sel,NULL,&iter))
	{
		int id;
		gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,1,&id,-1);
		gtk_list_store_remove(store,&iter);
		ColorScheme *cs=&g_array_index(schemes,ColorScheme,id);
		GtkWidget *w=g_array_index(menuitems,GtkWidget *,id);
		gtk_container_remove(GTK_CONTAINER(menu),w);
		cs->id=-1;
		saveSchemes();
	}
	
}

static void closeScheme()
{
}

uint32_t *getColorScheme(int id)
{
	static ColorScheme standard;
	if (id==-1)
	{
		initScheme(&standard);
		return standard.colors;
	}
	ColorScheme *cs=&g_array_index(schemes,ColorScheme,id);
	return cs->colors;
}
static ColorScheme *curScheme;
static void editScheme(GtkButton *widget,GtkListStore *store)
{
	GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(schemeList));
	GtkTreeIter iter;
	if (gtk_tree_selection_get_selected(sel,NULL,&iter))
	{
		int id;
		gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,1,&id,-1);
		curScheme=&g_array_index(schemes,ColorScheme,id);

		GtkWidget *win=gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title(GTK_WINDOW(win),"Color Scheme");
		gtk_window_set_default_size(GTK_WINDOW(win),350,400);
		g_signal_connect(G_OBJECT(win),"destroy",
			G_CALLBACK(closeScheme),NULL);

		//main vbox
		GtkWidget *vbox=gtk_vbox_new(FALSE,5);
		gtk_container_add(GTK_CONTAINER(win),vbox);

		GtkListStore *substore=gtk_list_store_new(4,G_TYPE_INT,G_TYPE_STRING,G_TYPE_INT,G_TYPE_INT);

		GtkTreeIter iter;
		for (int i=0;i<numBlocks;i++)
		{
			gtk_list_store_append(substore,&iter);
			gtk_list_store_set(substore,&iter,
				0,i,
				1,blocks[i].name,
				2,curScheme->colors[i]>>8,
				3,curScheme->colors[i]&0xff,
				-1);
		}

	
		colorList=gtk_tree_view_new_with_model(GTK_TREE_MODEL(substore));
		GtkWidget *scroll=gtk_scrolled_window_new(gtk_tree_view_get_hadjustment(GTK_TREE_VIEW(colorList)),gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(colorList)));
		gtk_box_pack_start(GTK_BOX(vbox),scroll,TRUE,TRUE,0);
		gtk_container_add(GTK_CONTAINER(scroll),colorList);

		GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(colorList));
		gtk_tree_selection_set_mode(sel,GTK_SELECTION_SINGLE);
		GtkCellRenderer *renderer;
		GtkTreeViewColumn *column;
		renderer=gtk_cell_renderer_text_new();
		column=gtk_tree_view_column_new_with_attributes("Id",
			renderer,"text",0,NULL);
		gtk_tree_view_column_set_cell_data_func(column,renderer,
			renderId,NULL,NULL);
		gtk_tree_view_append_column(GTK_TREE_VIEW(colorList),column);
		renderer=gtk_cell_renderer_text_new();
		column=gtk_tree_view_column_new_with_attributes("Name",
			renderer,"text",1,NULL);
		gtk_tree_view_append_column(GTK_TREE_VIEW(colorList),column);
		renderer=gtk_cell_renderer_text_new();
		column=gtk_tree_view_column_new_with_attributes("Color",
			renderer,"text",2,NULL);
		gtk_tree_view_column_set_cell_data_func(column,renderer,
			renderColor,NULL,NULL);
		g_object_set(renderer,"editable",TRUE,NULL);
		g_signal_connect(G_OBJECT(renderer),"edited",
			G_CALLBACK(updateColor),GTK_TREE_MODEL(substore));
		gtk_tree_view_append_column(GTK_TREE_VIEW(colorList),column);
		renderer=gtk_cell_renderer_text_new();
		column=gtk_tree_view_column_new_with_attributes("Alpha",
			renderer,"text",3,NULL);
		g_object_set(renderer,"editable",TRUE,NULL);
		g_signal_connect(G_OBJECT(renderer),"edited",
			G_CALLBACK(updateAlpha),GTK_TREE_MODEL(substore));
		gtk_tree_view_append_column(GTK_TREE_VIEW(colorList),column);

		gtk_widget_show_all(win);
	}
}
static void updateColor(GtkCellRendererText *renderer,gchar *path,gchar *text,GtkTreeModel *model)
{
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(model,&iter,path);
	int id;
	gtk_tree_model_get(model,&iter,0,&id,-1);
	uint32_t color=0;
	for (int i=0;text[i];i++)
	{
		if (text[i]>='0' && text[i]<='9')
		{
			color<<=4;
			color|=text[i]-'0';
		}
		if (text[i]>='a' && text[i]<='f')
		{
			color<<=4;
			color|=text[i]+10-'a';
		}
		if (text[i]>='A' && text[i]<='F')
		{
			color<<=4;
			color|=text[i]+10-'A';
		}
	}
	gtk_list_store_set(GTK_LIST_STORE(model),&iter,2,color,-1);
	curScheme->colors[id]&=0xff;
	curScheme->colors[id]|=color<<8;
	saveSchemes();
}
static void updateAlpha(GtkCellRendererText *renderer,gchar *path,gchar *text,GtkTreeModel *model)
{
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(model,&iter,path);
	int id;
	gtk_tree_model_get(model,&iter,0,&id,-1);
	uint8_t alpha=0;
	for (int i=0;text[i];i++)
	{
		if (text[i]>='0' && text[i]<='9')
		{
			alpha*=10;
			alpha+=text[i]-'0';
		}
	}
	gtk_list_store_set(GTK_LIST_STORE(model),&iter,3,alpha,-1);
	curScheme->colors[id]&=~0xff;
	curScheme->colors[id]|=alpha;
	saveSchemes();
}
static void renderId(GtkTreeViewColumn *column,GtkCellRenderer *cell,
	GtkTreeModel *model,GtkTreeIter *iter,gpointer data)
{
	int id;
	gtk_tree_model_get(model,iter,0,&id,-1);
	gchar *name=g_strdup_printf("%d.",id);
	g_object_set(G_OBJECT(cell),"text",name,NULL);
	g_free(name);
}
static void renderColor(GtkTreeViewColumn *column,GtkCellRenderer *cell,
	GtkTreeModel *model,GtkTreeIter *iter,gpointer data)
{
	uint32_t color;
	gtk_tree_model_get(model,iter,2,&color,-1);
	gchar *name=g_strdup_printf("#%06x",color);
	g_object_set(G_OBJECT(cell),"text",name,NULL);
	g_free(name);
}
