using System; using System.Collections.Generic; using System.IO; using System.Text; using System.Security.Cryptography; using System.Globalization; using System.IO.Compression;
public class StateCmp {
  class R { public byte[] d; public int p; }
  static int I16(R r){ int v=(r.d[r.p]<<8)|r.d[r.p+1]; r.p+=2; return v; }
  static int I32(R r){ int v=(r.d[r.p]<<24)|(r.d[r.p+1]<<16)|(r.d[r.p+2]<<8)|r.d[r.p+3]; r.p+=4; return v; }
  static string Str(R r){ int l=I16(r); string s=Encoding.UTF8.GetString(r.d,r.p,l); r.p+=l; return s; }
  static object Pay(R r,int t){
    switch(t){
      case 1: return (long)(sbyte)r.d[r.p++];
      case 2: { long v=(short)I16(r); return v; }
      case 3: return (long)I32(r);
      case 4: { long v=0; for(int i=0;i<8;i++) v=(v<<8)|r.d[r.p++]; return v; }
      case 5: r.p+=4; return null; case 6: r.p+=8; return null;
      case 7: { int n=I32(r); r.p+=n; return null; }
      case 8: return Str(r);
      case 9: { int et=r.d[r.p++]; int n=I32(r); var l=new List<object>(); for(int i=0;i<n;i++) l.Add(Pay(r,et)); return l; }
      case 10: { var m=new Dictionary<string,object>(); while(true){ int tt=r.d[r.p++]; if(tt==0) break; string nm=Str(r); m[nm]=Pay(r,tt); } return m; }
      case 11: { int n=I32(r); var a=new int[n]; for(int i=0;i<n;i++) a[i]=I32(r); return a; }
      case 12: { int n=I32(r); var a=new long[n]; for(int i=0;i<n;i++){ long v=0; for(int k=0;k<8;k++) v=(v<<8)|r.d[r.p++]; a[i]=v; } return a; }
    }
    throw new Exception("bad tag "+t);
  }
  static string StateKey(object ent, Dictionary<string,string> defaults){
    string name=null; Dictionary<string,object> props=null; var s=ent as string;
    if(s!=null) name=s;
    else { var m=(Dictionary<string,object>)ent; foreach(var k in new[]{"Name","id",""}) if(m.ContainsKey(k)&&m[k] is string){ name=(string)m[k]; break; }
      foreach(var k in new[]{"Properties","properties"}) if(m.ContainsKey(k)) props=(Dictionary<string,object>)m[k]; }
    string shortName=name.StartsWith("minecraft:")?name.Substring(10):name;
    var parts=new SortedDictionary<string,string>(StringComparer.Ordinal);
    if(props!=null){ foreach(var kv in props) parts[kv.Key]=Convert.ToString(kv.Value); }
    else { string d; if(defaults.TryGetValue(shortName,out d)) foreach(var kv in d.Split(',')){ var q=kv.Split('='); parts[q[0]]=q[1]; } }
    var sb=new StringBuilder(shortName); if(parts.Count>0){ sb.Append('['); bool first=true; foreach(var kv in parts){ if(!first) sb.Append(','); first=false; sb.Append(kv.Key+"="+kv.Value); } sb.Append(']'); }
    return sb.ToString();
  }
  // world (x,z) -> canonical state at block y; keys are "x,z"
  public static Dictionary<string,string> WorldStates(string regionDir, int blockY, Dictionary<string,string> defaults){
    var res=new Dictionary<string,string>();
    foreach(var f in Directory.GetFiles(regionDir,"r.*.*.mca")){
      var parts=Path.GetFileNameWithoutExtension(f).Split('.'); int rx=int.Parse(parts[1]), rz=int.Parse(parts[2]);
      byte[] b; using(var fs=new FileStream(f,FileMode.Open,FileAccess.Read,FileShare.ReadWrite)){ b=new byte[fs.Length]; fs.Read(b,0,b.Length); }
      for(int ci=0;ci<1024;ci++){
        int e=(b[ci*4]<<16)|(b[ci*4+1]<<8)|b[ci*4+2]; if(e==0) continue; int off=e*4096;
        int len=(b[off]<<24)|(b[off+1]<<16)|(b[off+2]<<8)|b[off+3]; if(b[off+4]!=2) continue;
        byte[] raw; using(var ms=new MemoryStream(b,off+5+2,len-3)) using(var ds=new DeflateStream(ms,CompressionMode.Decompress)) using(var o=new MemoryStream()){ ds.CopyTo(o); raw=o.ToArray(); }
        var r=new R{d=raw,p=0}; r.p++; Str(r); var root=(Dictionary<string,object>)Pay(r,10);
        int secY=(int)Math.Floor(blockY/16.0); int ly=((blockY%16)+16)%16;
        foreach(var so in (List<object>)root["sections"]){ var sec=(Dictionary<string,object>)so; if((long)sec["Y"]!=secY) continue;
          var bs=(Dictionary<string,object>)sec["block_states"]; var pal=(List<object>)bs["palette"]; if(!bs.ContainsKey("data")) continue; var data=(long[])bs["data"];
          int n=pal.Count; int bits=Math.Max(4,(int)Math.Ceiling(Math.Log(n,2))); int per=64/bits; var keys=new string[n]; for(int k=0;k<n;k++) keys[k]=StateKey(pal[k],defaults);
          for(int z=0;z<16;z++) for(int x=0;x<16;x++){ int t=ly*256+z*16+x; ulong val=(ulong)data[t/per]; int idx=(int)((val>>((t%per)*bits))&(ulong)((1L<<bits)-1)); string key=keys[idx]; if(key=="air"||key=="cave_air"||key=="void_air"||key=="barrier") continue;
            long wx=rx*512+(ci%32)*16+x, wz=rz*512+(ci/32)*16+z; res[wx+","+wz]=key; }
        }
      }
    }
    return res;
  }
  static int Find(int[] par,int i){ while(par[i]!=i){ par[i]=par[par[i]]; i=par[i]; } return i; }
  public class Comp { public double cx,cy,cz; public string sig; public string mat; public int faces; }
  public static List<Comp> Components(string file){
    var md5=MD5.Create(); var outl=new List<Comp>();
    var vx=new List<float>(); var vy=new List<float>(); var vz=new List<float>(); var faces=new List<int[]>(); var faceMat=new List<string>(); string mat="";
    foreach(var line in File.ReadLines(file)){
      if(line.Length<2) continue;
      if(line[0]=='v'&&line[1]==' '){ var p=line.Split(' '); vx.Add(float.Parse(p[1],CultureInfo.InvariantCulture)); vy.Add(float.Parse(p[2],CultureInfo.InvariantCulture)); vz.Add(float.Parse(p[3],CultureInfo.InvariantCulture)); }
      else if(line.StartsWith("usemtl ")) mat=line.Substring(7);
      else if(line[0]=='f'&&line[1]==' '){ var p=line.Split(' '); var idx=new int[p.Length-1]; for(int i=1;i<p.Length;i++){ int s=p[i].IndexOf('/'); idx[i-1]=int.Parse(s<0?p[i]:p[i].Substring(0,s))-1; } faces.Add(idx); faceMat.Add(mat); }
    }
    int nv=vx.Count; var par=new int[nv]; for(int i=0;i<nv;i++) par[i]=i;
    foreach(var f in faces) for(int i=1;i<f.Length;i++){ int a=Find(par,f[0]),b=Find(par,f[i]); if(a!=b) par[a]=b; }
    var comps=new Dictionary<int,List<int>>();
    for(int fi=0;fi<faces.Count;fi++){ int r=Find(par,faces[fi][0]); List<int> l; if(!comps.TryGetValue(r,out l)){ l=new List<int>(); comps[r]=l; } l.Add(fi); }
    foreach(var kv in comps){ var fl=kv.Value; float mx=float.MaxValue,my=float.MaxValue,mz=float.MaxValue,Mx=float.MinValue,Mz=float.MinValue;
      foreach(int fi in fl) foreach(int v in faces[fi]){ mx=Math.Min(mx,vx[v]); my=Math.Min(my,vy[v]); mz=Math.Min(mz,vz[v]); Mx=Math.Max(Mx,vx[v]); Mz=Math.Max(Mz,vz[v]); }
      var keys=new List<string>(); var mats=new SortedSet<string>();
      foreach(int fi in fl){ mats.Add(faceMat[fi]); var pts=new List<string>(); foreach(int v in faces[fi]) pts.Add(((int)Math.Round((vx[v]-mx)*64.0))+","+((int)Math.Round((vy[v]-my)*64.0))+","+((int)Math.Round((vz[v]-mz)*64.0))); pts.Sort(StringComparer.Ordinal); keys.Add(faceMat[fi]+"|"+string.Join(";",pts)); }
      keys.Sort(StringComparer.Ordinal); var hash=BitConverter.ToString(md5.ComputeHash(Encoding.UTF8.GetBytes(string.Join("\n",keys)))).Replace("-","").Substring(0,12);
      outl.Add(new Comp{ cx=(mx+Mx)/2, cy=my, cz=(mz+Mz)/2, sig=hash, mat=string.Join("+",mats), faces=fl.Count }); }
    return outl;
  }
}
