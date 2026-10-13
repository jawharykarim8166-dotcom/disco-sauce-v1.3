#include "../Source/dsp/AutoTune.h"
#include <cstdio>
#include <random>
#include <functional>
using namespace ds;

// instantaneous f0 trajectory (Hz) and amplitude envelope
struct Scenario { const char* name; double dur; std::function<double(double)> f0; std::function<double(double)> amp; };

static std::vector<float> synth(const Scenario& s,double sr,std::vector<double>& f0t){
  int n=(int)(s.dur*sr); std::vector<float> x(n); f0t.resize(n); double ph=0; std::mt19937 g(5); std::normal_distribution<double> nd(0,1);
  for(int i=0;i<n;i++){ double t=i/sr; double f=s.f0(t); f0t[i]=f; ph+=2*kPi*f/sr; if(ph>2*kPi*1000) ph-=2*kPi*1000;
    double y=0; int K=std::min(40,(int)(5000.0/f));
    for(int k=1;k<=K;k++){ double fk=k*f; double fg=std::exp(-std::pow((fk-700)/250,2))+0.7*std::exp(-std::pow((fk-1200)/300,2))+0.4*std::exp(-std::pow((fk-2600)/500,2))+0.05;
      y+=fg/k*std::sin(k*ph);} 
    x[i]=(float)(0.25*y*s.amp(t)+0.0004*nd(g)); }
  return x;
}
// independent reference pitch tracker (full-rate YIN, long window)
static double refPitch(const std::vector<float>& x,int c,double sr){
  int W=(int)(0.045*sr), tmax=(int)(sr/65), tmin=(int)(sr/1000); if(c<0||c+W+tmax>(int)x.size()) return 0;
  std::vector<double> d(tmax+2,0),cm(tmax+2,1); double e=0; for(int j=0;j<W;j++) e+=x[c+tmax/2+j]*(double)x[c+tmax/2+j]; if(std::sqrt(e/W)<1e-4) return 0;
  for(int t=1;t<=tmax;t++){double s=0; for(int j=0;j<W;j++){double v=x[c+j]-x[c+j+t]; s+=v*v;} d[t]=s;}
  double run=0; for(int t=1;t<=tmax;t++){run+=d[t]; cm[t]=run>0?d[t]*t/run:1;}
  for(int t=tmin;t<tmax;t++) if(cm[t]<0.1){ while(t+1<tmax&&cm[t+1]<cm[t])t++; double a=cm[t-1],b=cm[t],cc=cm[t+1],den=a-2*b+cc; double sh=std::abs(den)>1e-12?0.5*(a-cc)/den:0; return sr/(t+sh);} return 0;
}
static double nearestAllowed(double midi,int key,int scale){ int m=scaleMask(scale); double best=1e9,bn=0; int c=(int)std::lround(midi); for(int n=c-7;n<=c+7;n++){int pc=(((n-key)%12)+12)%12; if(!((m>>pc)&1))continue; double dd=std::abs(midi-n); if(dd<best){best=dd;bn=n;}} return bn; }
int main(int argc,char**argv){
  double sr= argc>2? atof(argv[2]) : 48000; 
  auto env=[](double base,double gain){ return [=](double t){ return gain; }; };
  std::vector<Scenario> sc={
   {"steady 225Hz detuned",3,[](double){return 225.0;},[](double){return 1.0;}},
   {"vibrato 5.5Hz +-40c @225",4,[](double t){return 225*std::pow(2.0,0.4/12*std::sin(2*kPi*5.5*t));},[](double){return 1.0;}},
   {"wide vibrato +-80c @300 (female)",4,[](double t){return 300*std::pow(2.0,0.8/12*std::sin(2*kPi*6*t));},[](double){return 1.0;}},
   {"low male 95Hz vibrato",4,[](double t){return 95*std::pow(2.0,0.35/12*std::sin(2*kPi*5*t));},[](double){return 1.0;}},
   {"high female 520Hz drift",4,[](double t){return 520*std::pow(2.0,(0.3*std::sin(2*kPi*0.7*t)+0.15)/12);},[](double){return 1.0;}},
   {"quiet -46dBFS vibrato",4,[](double t){return 225*std::pow(2.0,0.3/12*std::sin(2*kPi*5.5*t));},[](double){return 0.02;}},
   {"glide 200->300Hz over 0.6s",3,[](double t){ double u=std::min(1.0,std::max(0.0,(t-1.0)/0.6)); return 200*std::pow(1.5,u);},[](double){return 1.0;}},
   {"note steps (rap-ish)",4,[](double t){ static const double f[8]={196,233,247,208,262,175,220,294}; int k=(int)(t/0.5)%8; return f[k]*std::pow(2.0,0.25/12*std::sin(2*kPi*5.5*t));},[](double t){ double p=std::fmod(t,0.5); return p<0.03? p/0.03:1.0; }},
  };
  int key=0,scale=0; AutoTune::Settings s; s.amount=1; s.retuneMs=0; s.key=key; s.scale=scale;
  for(auto& S:sc){
    std::vector<double> f0t; auto in=synth(S,sr,f0t); int n=in.size(); std::vector<float> L=in,R=in,dl(n),dr(n);
    AutoTune at; if(argc>1) at.setDetLagMs(atof(argv[1])); at.prepare(sr); for(int i=0;i<n;i+=480){int m=std::min(480,n-i); at.process(&L[i],&R[i],&dl[i],&dr[i],m,s);}
    int dn=at.getLatency(); std::vector<double> errs; int tot=0,missed=0;
    for(int c=(int)(0.7*sr); c+(int)(0.045*sr)+(int)(sr/65)<n; c+=(int)(0.01*sr)){
      int mid=c+(int)(0.0225*sr)+(int)(sr/65)/2; // centre of reference window
      double f0in=f0t[std::max(0,mid-dn)]; if(S.amp(mid/sr)<=0.0) continue;
      double fo=refPitch(L,c,sr); ++tot; if(fo<=0){++missed;continue;}
      // expected note = nearest allowed to the input pitch averaged over ~one vibrato cycle (+-90 ms)
      int c0=std::max(0,mid-dn-(int)(0.09*sr)), c1=std::min(n-1,mid-dn+(int)(0.09*sr)); double sum=0,mn=1e9,mx=-1e9; int cnt=0;
      for(int q=c0;q<=c1;q+=48){ double m=69+12*std::log2(f0t[q]/440.0); sum+=m; mn=std::min(mn,m); mx=std::max(mx,m); cnt++; }
      if(mx-mn>1.3){ --tot; continue; }   // transition zone, not scored
      double mo=69+12*std::log2(fo/440.0); double tgt=nearestAllowed(sum/cnt,key,scale);
      errs.push_back((mo-tgt)*100.0); }
    std::sort(errs.begin(),errs.end(),[](double a,double b){return std::abs(a)<std::abs(b);});
    double mean=0; for(double e:errs) mean+=std::abs(e); mean/=std::max<size_t>(1,errs.size());
    double p95=errs.empty()?0:std::abs(errs[(size_t)(errs.size()*0.95)]);
    // uncorrected reference error for context
    printf("%-34s mean|err| %5.1f c  p95 %5.1f c  untracked %d/%d\n",S.name,mean,p95,missed,tot);
  }
}
