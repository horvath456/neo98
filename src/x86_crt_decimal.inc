// SPDX-License-Identifier: GPL-2.0-or-later
unsigned Application::CrtFloatApi(ApiName name,const unsigned*a){
 if(!a[0]){Fail("Null CRT floating conversion input");return 0;}
 // Keep input stable if publishing locale data ever suspends a guest fiber.
 char text[8192];const char*input=ReadString(a[0]);if(state==Failed)return 0;strcpy(text,input);
 unsigned conv=LocaleApi("localeconv",nullptr);if(!conv||state==Failed)return 0;
 unsigned decimal_address=Read32(conv);unsigned char decimal=0;if(!decimal_address||!Read(decimal_address,&decimal,1)||!decimal){Fail("CRT numeric locale has no decimal separator");return 0;}
 struct Environment {
  Application&app;const char*text;unsigned decimal;
  unsigned Byte(unsigned at){return static_cast<unsigned char>(text[at]);}
  bool Class(unsigned c,unsigned flag){const unsigned args[]={c,flag};return app.LocaleApi("_isctype",args)!=0;}
  bool Digit(unsigned c){return Class(c,4);}bool Space(unsigned c){return Class(c,8);}
  unsigned Decimal(){return decimal;}
 }e{*this,text,decimal};
 bool atof=API_IS(name,"atof");const auto result=Win98CrtDecimal::Convert(e,0,atof);
 if(state==Failed)return 0;
 if(!atof&&a[1]){unsigned end=a[0]+result.end;if(!Store(a[1],&end,4))return 0;}
 if(result.range)SetCrtErrno(34);
 if(state!=Failed)ReturnDoubleBits(result.bits);
 return 0;
}
