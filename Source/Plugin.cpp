#include <JuceHeader.h>
#include <array>
#include <cmath>
using namespace juce;
using AP=AudioProcessorValueTreeState;
static const std::array<std::array<int,3>,6> routes{{{{0,1,2}},{{0,2,1}},{{1,0,2}},{{1,2,0}},{{2,0,1}},{{2,1,0}}}};
static const StringArray names{"COMP","EQ","TRANSIENT"};
class Engine: public AudioProcessor {
public:
 AP p{*this,nullptr,"Tominator",layout()};
 std::atomic<float> gr{0};
 static AP::ParameterLayout layout(){
 AP::ParameterLayout l;
 auto f=[&](String id,String name,float a,float b,float d,float step=0.1f,float skew=1.f){l.add(std::make_unique<AudioParameterFloat>(ParameterID{id,1},name,NormalisableRange<float>(a,b,step,skew),d));};
 auto b=[&](String id,String n){l.add(std::make_unique<AudioParameterBool>(ParameterID{id,1},n,false));};
 auto c=[&](String id,String n,StringArray a,int d=0){l.add(std::make_unique<AudioParameterChoice>(ParameterID{id,1},n,a,d));};
 f("input","Input",-24,24,0); f("output","Output",-24,24,0);
 b("comp","Compressor in"); b("eq","EQ in"); b("trans","Transient in");
 f("threshold","Threshold",-60,0,-18); f("attack","Attack ms",1,30,15); f("release","Release ms",50,200,100);
 c("ratio","Ratio",{"3:1","4:1","5:1","6:1"},1); c("knee","Knee",{"Hard","Soft 8 dB"}); f("makeup","Makeup",-12,24,0);
 c("hp","HPF",{"Off","30","60","100","150"}); c("lp","LPF",{"Off","20k","16k","12k","10k"});
 float lo[]={0,100,600,2000,6000},hi[]={500,1000,3000,8000,20000},def[]={100,300,1200,4000,12000};
 for(int i=0;i<5;++i){auto k=String(i); f("freq"+k,"Frequency "+k,lo[i],hi[i],def[i],1,.45f);f("gain"+k,"Gain "+k,-16,16,0);if(i>0&&i<4)f("q"+k,"Q "+k,.15f,12,1,.01f,.35f);}
 f("punch","Transient",-15,15,0);f("sustain","Sustain",-15,15,0);
 c("order","Order",{"COMP EQ TRANSIENT","COMP TRANSIENT EQ","EQ COMP TRANSIENT","EQ TRANSIENT COMP","TRANSIENT COMP EQ","TRANSIENT EQ COMP"});
 return l;}
 Engine():AudioProcessor(BusesProperties().withInput("Input",AudioChannelSet::stereo(),true).withOutput("Output",AudioChannelSet::stereo(),true)){}
 float v(String id){return p.getRawParameterValue(id)->load();}
 const String getName()const override{return "Tominator";} bool acceptsMidi()const override{return false;} bool producesMidi()const override{return false;} double getTailLengthSeconds()const override{return 0;}
 int getNumPrograms()override{return 1;} int getCurrentProgram()override{return 0;} void setCurrentProgram(int)override{} const String getProgramName(int)override{return {};} void changeProgramName(int,const String&)override{}
 bool isBusesLayoutSupported(const BusesLayout& l)const override{return l.getMainInputChannelSet()==l.getMainOutputChannelSet()&&(l.getMainOutputChannelSet()==AudioChannelSet::mono()||l.getMainOutputChannelSet()==AudioChannelSet::stereo());}
 struct Biquad{double b0=1,b1=0,b2=0,a1=0,a2=0,z1[2]{},z2[2]{};void set(const dsp::IIR::Coefficients<double>& c){auto* a=c.getRawCoefficients();b0=a[0];b1=a[1];b2=a[2];a1=a[3];a2=a[4];} float run(float x,int c){double y=b0*x+z1[c];z1[c]=b1*x-a1*y+z2[c];z2[c]=b2*x-a2*y;return float(y);}void reset(){for(int i=0;i<2;++i)z1[i]=z2[i]=0;}};
 std::array<Biquad,7> filters; std::array<bool,7> active{}; double sr=44100,level=0,fast=0,slow=0,body=0; bool wasComp=false,wasEQ=false,wasTrans=false;
 void prepareToPlay(double s,int)override{sr=s;level=fast=slow=body=0;for(auto& f:filters)f.reset();active.fill(false);wasComp=wasEQ=wasTrans=false;} void releaseResources()override{}
 void processBlock(AudioBuffer<float>& buf,MidiBuffer&)override{
 ScopedNoDenormals nd;auto channels=buf.getNumChannels(),n=buf.getNumSamples();
 float in=v("input"),out=v("output"); if(in!=0)buf.applyGain(Decibels::decibelsToGain(in));
 bool enabled[]={v("comp")>0.5f,v("eq")>0.5f,v("trans")>0.5f};
 if(!enabled[0]){level=0;gr=0;}if(!enabled[1]&&wasEQ){for(auto& f:filters)f.reset();active.fill(false);}if(!enabled[2])fast=slow=body=0;
 for(int id:routes[size_t(jlimit(0,5,int(v("order"))))]){
 if(!enabled[id])continue;
 if(id==0){double a=std::exp(-1/(sr*v("attack")*.001)),r=std::exp(-1/(sr*v("release")*.001)),t=v("threshold"),ratio=3+v("ratio"),w=v("knee")>0?8:0,make=v("makeup");
 for(int i=0;i<n;++i){float peak=0;for(int c=0;c<channels;++c)peak=jmax(peak,std::abs(buf.getSample(c,i)));double x=Decibels::gainToDecibels(double(peak),-120.0)-t;double over=w>0&&x>-w*.5&&x<w*.5?(x+w*.5)*(x+w*.5)/(2*w):std::max(0.0,x);double target=over*(1-1/ratio);double coeff=target>level?a:r;level=coeff*level+(1-coeff)*target;float gain=float(Decibels::decibelsToGain(make-level));for(int c=0;c<channels;++c)buf.setSample(c,i,buf.getSample(c,i)*gain);}gr=float(level);}
 if(id==1){using C=dsp::IIR::Coefficients<double>;auto freq=[&](double f){return jlimit(1.0,sr*.475,f);};auto set=[&](int j,bool on,C::Ptr coeff){if(on&&coeff)filters[j].set(*coeff);if(on!=active[j])filters[j].reset();active[j]=on;};
 int h=int(v("hp")),l=int(v("lp"));double hf[]={30,30,60,100,150},lf[]={20000,20000,16000,12000,10000};
 set(0,h!=0,h?C::makeHighPass(sr,freq(hf[h]),.70710678):nullptr);set(6,l!=0,l?C::makeLowPass(sr,freq(lf[l]),.70710678):nullptr);
 for(int j=0;j<5;++j){String k(j);double g=v("gain"+k),f=v("freq"+k),a=Decibels::decibelsToGain(g);C::Ptr co;if(g!=0&&f>0){if(j==0)co=C::makeLowShelf(sr,freq(f),.70710678,a);else if(j==4)co=C::makeHighShelf(sr,freq(f),.70710678,a);else {double q=jlimit(.1,24.0,double(v("q"+k)));co=C::makePeakFilter(sr,freq(f),q,a);}}set(j+1,g!=0&&f>0,co);}
 for(int j=0;j<7;++j)if(active[j])for(int c=0;c<channels;++c){auto* x=buf.getWritePointer(c);for(int i=0;i<n;++i)x[i]=filters[j].run(x[i],c);}}
 if(id==2){double punch=v("punch"),sus=v("sustain");if(punch==0&&sus==0){fast=slow=body=0;continue;}double fa=std::exp(-1/(sr*.001)),fr=std::exp(-1/(sr*.025)),sa=std::exp(-1/(sr*.025)),rr=std::exp(-1/(sr*.12)),ba=std::exp(-1/(sr*.008));
 for(int i=0;i<n;++i){double x=0;for(int c=0;c<channels;++c)x=std::max(x,double(std::abs(buf.getSample(c,i))));double a=x>fast?fa:fr,b=x>slow?sa:rr;fast=a*fast+(1-a)*x;slow=b*slow+(1-b)*x;double onset=jlimit(0.0,1.0,(fast-slow)/(fast+1e-8));body=ba*body+(1-ba)*jlimit(0.0,1.0,(slow-fast*.35)/(slow+1e-8));float gain=float(Decibels::decibelsToGain(punch*onset+sus*body*(1-onset)));for(int c=0;c<channels;++c)buf.setSample(c,i,buf.getSample(c,i)*gain);}}
 }
 wasComp=enabled[0];wasEQ=enabled[1];wasTrans=enabled[2];if(out!=0)buf.applyGain(Decibels::decibelsToGain(out));
 }
 void getStateInformation(MemoryBlock& b)override{auto xml=p.copyState().createXml();copyXmlToBinary(*xml,b);}void setStateInformation(const void* d,int n)override{if(auto xml=getXmlFromBinary(d,n))if(xml->hasTagName(p.state.getType()))p.replaceState(ValueTree::fromXml(*xml));}
 bool hasEditor()const override{return true;} AudioProcessorEditor* createEditor()override;
};
class RackLook:public LookAndFeel_V4{
public:
 RackLook(){setColour(Slider::textBoxTextColourId,Colour(0xffeee9dc));setColour(Slider::textBoxBackgroundColourId,Colour(0xff141619));setColour(Slider::textBoxOutlineColourId,Colour(0xff4b4b49));setColour(ComboBox::backgroundColourId,Colour(0xff151719));setColour(ComboBox::outlineColourId,Colour(0xff83817a));setColour(ComboBox::textColourId,Colours::ivory);}
 void drawRotarySlider(Graphics& g,int x,int y,int w,int h,float pos,float start,float end,Slider& s)override{
 float r=jmin(w,h)*.34f,cx=x+w*.5f,cy=y+h*.5f;Rectangle<float> b(cx-r,cy-r,2*r,2*r);
 for(int i=0;i<=10;++i){float a=start+(end-start)*i/10;g.setColour(Colour(0xffe0ded4));g.drawLine(cx+std::sin(a)*(r+6),cy-std::cos(a)*(r+6),cx+std::sin(a)*(r+10),cy-std::cos(a)*(r+10),i==5?1.6f:1.f);}
 g.setColour(Colours::black.withAlpha(.8f));g.fillEllipse(b.translated(1,4).expanded(3));
 g.setGradientFill(ColourGradient(Colour(0xffe2e2dc),cx-r,cy-r,Colour(0xff25272a),cx+r,cy+r,false));g.fillEllipse(b.expanded(2));
 g.setColour(Colour(0xff17191b));g.fillEllipse(b);
 auto base=s.findColour(Slider::rotarySliderFillColourId);auto face=b.reduced(3);
 ColourGradient grad(base.brighter(.75f),cx-r,cy-r,base.darker(.55f),cx+r,cy+r,false);grad.addColour(.46,base.brighter(.18f));grad.addColour(.52,base);g.setGradientFill(grad);g.fillEllipse(face);
 for(int i=0;i<100;++i){float a=i*MathConstants<float>::twoPi/100;g.setColour(Colours::white.withAlpha(.025f+ .025f*std::abs(std::sin(a))));g.drawLine(cx+std::sin(a)*2,cy-std::cos(a)*2,cx+std::sin(a)*(r-4),cy-std::cos(a)*(r-4),.6f);}
 float a=start+pos*(end-start);g.setColour(Colours::black.withAlpha(.75f));g.drawLine(cx+std::sin(a)*r*.25f+1,cy-std::cos(a)*r*.25f+1,cx+std::sin(a)*r*.88f+1,cy-std::cos(a)*r*.88f+1,3);g.setColour(Colours::ivory);g.drawLine(cx+std::sin(a)*r*.25f,cy-std::cos(a)*r*.25f,cx+std::sin(a)*r*.88f,cy-std::cos(a)*r*.88f,2);
 }
 void drawButtonBackground(Graphics& g,Button& b,const Colour&,bool hover,bool down)override{
 auto box=b.getLocalBounds().toFloat().reduced(1);bool lit=b.getToggleState();g.setColour(Colours::black);g.fillRoundedRectangle(box,3);box=box.reduced(2);
 auto top=lit?Colour(0xffffd181):Colour(0xff626666);auto bot=lit?Colour(0xffb96c1e):Colour(0xff232526);if(hover)top=top.brighter(.15f);if(down)top=top.darker(.2f);
 g.setGradientFill(ColourGradient(top,0,box.getY(),bot,0,box.getBottom(),false));g.fillRoundedRectangle(box,2);g.setColour(lit?Colour(0xffffe9ad):Colour(0xff90918b));g.drawRoundedRectangle(box.reduced(.5f),2,1);
 }
 void drawButtonText(Graphics& g,TextButton& b,bool,bool)override{g.setFont(Font(12.f,Font::bold));g.setColour(b.getToggleState()?Colour(0xff2a1b0d):Colour(0xfff3efe2));g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(2),Justification::centred,1);}
};
class Editor:public AudioProcessorEditor,private Timer{
 Engine& e; RackLook look;
 std::vector<std::unique_ptr<Slider>> knobs;
 std::vector<std::unique_ptr<Label>> labels;
 std::vector<std::unique_ptr<TextButton>> buttons;
 std::vector<std::unique_ptr<AP::SliderAttachment>> sa;
 std::vector<std::unique_ptr<AP::ButtonAttachment>> ba;
 float meter=0; ComboBox order; int maskLast=-1,routeLast=-1; std::vector<int> map;
 void label(String t,int x,int y,int w,int h){auto z=std::make_unique<Label>();z->setText(t,dontSendNotification);z->setJustificationType(Justification::centred);z->setColour(Label::textColourId,Colour(0xffede8dc));z->setFont(Font(12.f,Font::bold));z->setBounds(x,y,w,h);addAndMakeVisible(*z);labels.push_back(std::move(z));}
 void knob(String id,String caption,int x,int y,int w=74,int h=90,Colour colour=Colour(0xff4e555b)){
 auto z=std::make_unique<Slider>();z->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);z->setRotaryParameters(3.9f,8.65f,true);z->setTextBoxStyle(Slider::TextBoxBelow,false,w-6,18);z->setColour(Slider::rotarySliderFillColourId,colour);z->setBounds(x,y+14,w,h-14);addAndMakeVisible(*z);label(caption,x,y,w,15);sa.push_back(std::make_unique<AP::SliderAttachment>(e.p,id,*z));knobs.push_back(std::move(z));}
 void toggle(String id,String title,int x,int y,int w=72){auto z=std::make_unique<TextButton>(title);z->setClickingTogglesState(true);z->setColour(TextButton::buttonColourId,Colour(0xff24292c));z->setColour(TextButton::buttonOnColourId,Colour(0xff386e48));z->setBounds(x,y,w,25);addAndMakeVisible(*z);ba.push_back(std::make_unique<AP::ButtonAttachment>(e.p,id,*z));buttons.push_back(std::move(z));}
 void choices(String id,StringArray a,int x,int y,int w){for(int i=0;i<a.size();++i){auto z=std::make_unique<TextButton>(a[i]);z->setBounds(x+i*w,y,w-3,24);z->setColour(TextButton::buttonColourId,Colour(0xff272d33));z->setColour(TextButton::buttonOnColourId,Colour(0xff497c86));auto* ptr=z.get();z->onClick=[this,id,i]{auto* p=e.p.getParameter(id);p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(float(i)));p->endChangeGesture();};addAndMakeVisible(*z);choiceButtons.push_back({ptr,id,i});buttons.push_back(std::move(z));}}
 struct ChoiceButton{TextButton* b;String id;int index;};std::vector<ChoiceButton> choiceButtons;
 void timerCallback()override{for(auto& c:choiceButtons)c.b->setToggleState(int(e.v(c.id))==c.index,dontSendNotification);int m=(e.v("comp")>.5f?1:0)|(e.v("eq")>.5f?2:0)|(e.v("trans")>.5f?4:0),r=int(e.v("order"));if(m!=maskLast||r!=routeLast){maskLast=m;routeLast=r;order.clear(dontSendNotification);map.clear();StringArray seen;String selected;for(int k=0;k<6;++k){String text;for(int id:routes[k])if(m&(1<<id)){if(text.isNotEmpty())text+=" > ";text+=names[id];}if(text.isEmpty())text="All bypassed";if(k==r)selected=text;if(!seen.contains(text)){seen.add(text);map.push_back(k);order.addItem(text,int(map.size()));}}order.setSelectedItemIndex(seen.indexOf(selected),dontSendNotification);order.setEnabled(map.size()>1);}meter += (e.gr.load()-meter)*(e.gr.load()>meter?.55f:.18f);repaint(1065,135,290,142);repaint(35,150,200,430);}
public:
 Editor(Engine& a):AudioProcessorEditor(a),e(a){setLookAndFeel(&look);setSize(1400,540);
 knob("input","INPUT dB",65,43,85,82,Colour(0xffa4a7a8));knob("output","OUTPUT dB",1250,43,85,82,Colour(0xffa4a7a8));
 label("HPF  Hz",220,48,300,18);choices("hp",{"OFF","30","60","100","150"},220,77,60);
 label("LPF  Hz",880,48,300,18);choices("lp",{"OFF","20k","16k","12k","10k"},880,77,60);
 label("PROCESSOR ORDER",540,48,320,18);addAndMakeVisible(order);order.setBounds(540,77,320,29);order.onChange=[this]{int i=order.getSelectedItemIndex();if(i>=0&&i<int(map.size())){auto* p=e.p.getParameter("order");p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(float(map[i])));p->endChangeGesture();}};
 toggle("comp","IN / BYPASS",65,217,142);knob("threshold","THRESHOLD",255,151,112,112,Colour(0xffa4a7a8));knob("ratio","RATIO",383,151,112,112,Colour(0xff343638));knob("attack","ATTACK ms",511,151,112,112,Colour(0xffa4a7a8));knob("release","RELEASE ms",639,151,112,112,Colour(0xffa4a7a8));knob("makeup","MAKEUP dB",940,151,112,112,Colour(0xffa4a7a8));label("KNEE",781,154,130,16);choices("knee",{"HARD"},793,179,107);choices("knee",{"HARD","SOFT 8 dB"},686,207,107);choiceButtons[choiceButtons.size()-2].b->setVisible(false);choiceButtons.back().b->setBounds(793,211,104,24);
 toggle("eq","IN / BYPASS",65,349,142);StringArray band{"LOW SHELF","LOW MID","MID","HIGH MID","HIGH SHELF"};for(int i=0;i<5;++i){int x=245+i*220;label(band[i],x,286,204,18);bool mid=i>0&&i<4;knob("freq"+String(i),"Hz",x+(mid?0:23),305,68,98,mid?Colour(0xff285b8c):Colour(0xff92342e));knob("gain"+String(i),"dB",x+(mid?68:108),305,68,98,Colour(0xffa4a7a8));if(mid){knob("q"+String(i),"Q",x+136,305,68,98,Colour(0xff343638));label("BROAD / NARROW",x+125,403,92,13);}}
 toggle("trans","IN / BYPASS",65,479,142);knob("punch","ATTACK dB",325,433,130,100,Colour(0xffa4a7a8));knob("sustain","SUSTAIN dB",620,433,130,100,Colour(0xffa4a7a8));startTimerHz(30);}
 ~Editor()override{stopTimer();setLookAndFeel(nullptr);}
 void paint(Graphics& g)override{
 g.fillAll(Colour(0xff101113));
 auto screw=[&](int x,int y){g.setColour(Colours::black);g.fillEllipse(float(x-6),float(y-5),12,12);g.setGradientFill(ColourGradient(Colour(0xff8b8b84),float(x-4),float(y-4),Colour(0xff232526),float(x+4),float(y+4),false));g.fillEllipse(float(x-5),float(y-5),10,10);g.setColour(Colour(0xff131415));g.drawLine(float(x-3),float(y),float(x+3),float(y),1.5f);g.drawLine(float(x),float(y-3),float(x),float(y+3),1.5f);};
 auto panel=[&](int y,int h,Colour c){g.setGradientFill(ColourGradient(c.brighter(.18f),0,float(y),c.darker(.22f),0,float(y+h),false));g.fillRect(34,y,1332,h);Random rng(123+y);for(int j=0;j<22000;++j){g.setColour((j%2?Colours::white:Colours::black).withAlpha(.045f));float x=34+rng.nextFloat()*1331;float yy=y+rng.nextFloat()*h;g.drawHorizontalLine(int(yy),x,x+1+rng.nextFloat()*4);}
 g.setColour(Colour(0xff868680));g.drawHorizontalLine(y,34,1366);g.setColour(Colours::black);g.drawHorizontalLine(y+h-1,34,1366);
 for(int x:{3,1366}){g.setGradientFill(ColourGradient(Colour(0xff4f5152),float(x),0,Colour(0xff222426),float(x+31),0,false));g.fillRect(x,y,31,h);for(int yy:{y+15,y+h-23}){g.setColour(Colour(0xff81817c));g.fillRoundedRectangle(float(x+5),float(yy),23,12,6);g.setColour(Colour(0xff08090a));g.fillRoundedRectangle(float(x+6),float(yy+1),21,10,5);}}
 for(int x:{47,1353})for(int yy:{y+12,y+h-12})screw(x,yy);};
 panel(1,130,Colour(0xff252729));panel(134,143,Colour(0xff282a2c));panel(280,140,Colour(0xff303c49));panel(423,116,Colour(0xff282a2b));
 g.setColour(Colour(0xffeee9da));g.setFont(Font(32.f,Font::bold|Font::italic));g.drawText("Tominator",66,5,280,36,Justification::centredLeft);g.setFont(Font(13.f,Font::italic));g.drawText("TOM PROCESSING RACK",330,10,300,25,Justification::centredLeft);g.setColour(Colour(0xff92372e));g.fillRect(70,39,200,2);
 auto line=[&](int x,int y,int h){g.setColour(Colour(0xff151719));g.drawVerticalLine(x,float(y),float(y+h));g.setColour(Colour(0xff838681).withAlpha(.6f));g.drawVerticalLine(x+1,float(y),float(y+h));};line(230,145,120);line(230,290,120);line(230,434,96);for(int i=1;i<5;++i)line(235+i*220,291,115);
 auto module=[&](String name,String id,int y){bool on=e.v(id)>.5f;g.setColour(Colour(0xff111213));g.fillRoundedRectangle(64.f,float(y),22.f,22.f,3);g.setGradientFill(ColourGradient(on?Colour(0xffffe2a0):Colour(0xff555047),65.f,float(y),on?Colour(0xffc77a23):Colour(0xff292925),85.f,float(y+22),false));g.fillRoundedRectangle(66.f,float(y+2),18.f,18.f,2);g.setColour(Colour(0xffeee9da));g.setFont(Font(15.f,Font::bold));g.drawFittedText(name,93,y-4,135,32,Justification::centredLeft,2);};
 module("COMPRESSOR","comp",167);module("EQ  /  5 BAND","eq",299);module("TRANSIENT DESIGNER","trans",437);
 Rectangle<float> m(1073,148,266,112);g.setColour(Colours::black);g.fillRoundedRectangle(m.expanded(4),5);g.setGradientFill(ColourGradient(Colour(0xfff8e3ab),m.getCentreX(),m.getY(),Colour(0xffbda46d),m.getCentreX(),m.getBottom(),false));g.fillRoundedRectangle(m,3);g.setColour(Colour(0xff30291e));g.setFont(Font(12.f,Font::bold));g.drawText("GAIN REDUCTION",1075,153,262,16,Justification::centred);
 float cx=1206,cy=270,rad=91;auto angle=[](float v){return -.99f+1.98f*jlimit(0.f,20.f,v)/20.f;};
 for(int i=0;i<=20;++i){float a=angle(float(i));g.setColour(i>=15?Colour(0xffa82b20):Colour(0xff372c20));float len=i%2==0?10.f:5.f;g.drawLine(cx+std::sin(a)*rad,cy-std::cos(a)*rad,cx+std::sin(a)*(rad-len),cy-std::cos(a)*(rad-len),i%2==0?1.5f:1.f);if(i%4==0){g.setFont(10.f);g.drawText(String(i),int(cx+std::sin(a)*(rad+12)-11),int(cy-std::cos(a)*(rad+12)-6),22,12,Justification::centred);}}
 float a=angle(meter);g.saveState();g.reduceClipRegion(m.toNearestInt());g.setColour(Colour(0xff292118));g.drawLine(cx,cy,cx+std::sin(a)*(rad-5),cy-std::cos(a)*(rad-5),1.7f);g.restoreState();g.setColour(Colour(0xff30291e));g.setFont(Font(12.f,Font::bold));g.drawText(String(meter,1)+" dB",1150,239,112,16,Justification::centred);
 g.setColour(Colour(0xff111416));g.fillRoundedRectangle(880,440,452,83,4);g.setColour(Colour(0xff666b6b));g.drawRoundedRectangle(880,440,452,83,4,1);g.setColour(Colour(0xffd0d5d5));g.setFont(Font(18.f,Font::bold));g.drawText("TRANSIENT CONTROL",890,451,432,26,Justification::centred);g.setFont(12.f);g.drawText("CLEAN DIGITAL ENVELOPE SHAPING",890,482,432,21,Justification::centred);
 }

};
AudioProcessorEditor* Engine::createEditor(){return new Editor(*this);}
AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Engine();}
