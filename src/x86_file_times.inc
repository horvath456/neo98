// SPDX-License-Identifier: GPL-2.0-or-later
unsigned Application::FileTimeApi(ApiName name,const unsigned*a){
 const bool set=API_IS(name,"SetFileTime");
 for(unsigned i=1;i<4;++i)if(a[i]&&!virtual_memory.Access(a[i],8,!set)){last_error=87;return 0;}
 struct E {
  Application&app;unsigned bias;
  void Lock(){}void Unlock(){}unsigned EnterFile(unsigned){return 0;}void LeaveFile(unsigned){}void Release(unsigned){}
  void Error(unsigned error){app.last_error=error;}
  unsigned Resolve(unsigned handle,unsigned access){unsigned error=app.files.ValidateTimeHandle(handle,access!=0);if(error){Error(error);return 0;}return handle;}
  unsigned DosHandle(unsigned handle){return handle;}
  unsigned Get(unsigned h,unsigned i,Win98FileTimes::Dos&out){Files::TimeStamp stamp;unsigned error=app.files.GetTime(h,i,stamp);out={stamp.date,stamp.time,stamp.hundredths};return error;}
  unsigned Set(unsigned h,unsigned i,const Win98FileTimes::Dos&stamp){return app.files.SetTime(h,i,{stamp.date,stamp.time,stamp.hundredths});}
  Win98Time::Q ReadFileTime(unsigned p){return Win98Time::Q(app.Read32(p))|(Win98Time::Q(app.Read32(p+4))<<32);}
  void WriteFileTime(unsigned p,Win98Time::Q value){app.Write32(p,unsigned(value));app.Write32(p+4,unsigned(value>>32));}
  bool ToDos(Win98Time::Q value,Win98FileTimes::Dos&stamp){unsigned error=Win98Time::DosService(0,stamp.date,stamp.time,stamp.hundredths,value,bias);if(error)Error(error);return !error;}
  bool FromDos(Win98FileTimes::Dos stamp,Win98Time::Q&value){unsigned error=Win98Time::DosService(1,stamp.date,stamp.time,stamp.hundredths,value,bias);if(error)Error(error);return !error;}
 }e{*this,EffectiveTimeBias()};
 return Win98FileTimes::Run(e,a,set);
}
