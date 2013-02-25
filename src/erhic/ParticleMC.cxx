//
// ParticleMC.cxx
//
// Created by TB on 10/10/11.
// Copyright 2011 BNL. All rights reserved.
//

#include <iostream>
#include <sstream>
#include <stdexcept>

#include <TLorentzRotation.h>
#include <TRotation.h>

#include "eicsmear/erhic/EventBase.h"
#include "eicsmear/functions.h"

namespace {

/*
 Returns the boost to transform to the rest frame of the vector "rest".
 If z is non-NULL, rotate the frame so that z AFTER BOOSTING
 defines the positive z direction of that frame.
 e.g. To boost a gamma-p interaction from the lab frame to the
 proton rest frame with the virtual photon defining z:
    computeBoost(protonLab, photonLab);
*/
TLorentzRotation computeBoost(const TLorentzVector& rest,
                              const TLorentzVector* z) {
   TLorentzRotation toRest(-(rest.BoostVector()));
   if(z) {
      TRotation rotate;
      TLorentzVector boostedZ(*z);
      boostedZ *= toRest;
      rotate.SetZAxis(boostedZ.Vect());
      // We need the rotation of the frame, so take the inverse.
      // See the TRotation documentation for more explanation.
      rotate = rotate.Inverse();
      toRest.Transform(rotate);
   } // if
   return toRest;
}

} // anonymous namespace

namespace erhic {

ParticleMC::ParticleMC(const std::string& line)
: I(-1)
, KS(-1)
, id(std::numeric_limits<Int_t>::min())
, orig(-1)
, daughter(-1)
, ldaughter(-1)
, px(0.)
, py(0.)
, pz(0.)
, E(0.)
, m(0.)
, pt(0.)
, xv(0.)
, yv(0.)
, zv(0.)
, parentId(std::numeric_limits<Int_t>::min())
, p(0.)
, theta(0.)
, phi(0.)
, rapidity(0.)
, eta(0.)
, z(0.)
, xFeynman(0.)
, thetaGamma(0.)
, ptVsGamma(0.)
, phiPrf(0.) {
   // Initialise to nonsense values to make input errors easy to spot
   if(not line.empty()) {
      static std::stringstream ss;
      ss.str("");
      ss.clear();
      ss << line;
      ss >>
      I >> KS >> id >> orig >> daughter >> ldaughter >>
      px >> py >> pz >> E >> m >> xv >> yv >> zv;
      // We should have no stream errors and should have exhausted
      // the whole of the stream filling the particle.
      if(ss.fail() or not ss.eof()) {
         throw std::runtime_error("Bad particle input: " + line);
      } // if
      ComputeDerivedQuantities();
   } // if
}

ParticleMC::~ParticleMC() {
}

void ParticleMC::Print(Option_t*) const {
   std::cout << I << '\t' << KS << '\t' << id << '\t' << orig << '\t' <<
   daughter << '\t' << ldaughter << '\t' << px << '\t' << py << '\t' << pz
   << '\t' << E << '\t' << m << '\t' << xv << '\t' << yv << '\t' << zv <<
   std::endl;
}

void ParticleMC::ComputeDerivedQuantities() {
   // Calculate quantities that depend only on the properties already read.
   pt = sqrt(pow(px, 2.) + pow(py, 2.));
   p = sqrt(pow(pt, 2.) + pow(pz, 2.));
   // Rapidity and pseudorapidity
   Double_t Epluspz = E + pz;
   Double_t Eminuspz = E - pz;
   Double_t Ppluspz = p + pz;
   Double_t Pminuspz = p - pz;
   if(Eminuspz <= 0.0 or Pminuspz == 0.0 or
      Ppluspz == 0.0 or Epluspz <= 0.0) {
      // Dummy values to avoid zero or infinite arguments in calculations
      rapidity = -19.;
      eta = -19.;
   }	//	if...
   else {
      rapidity = 0.5 * log(Epluspz / Eminuspz);
      eta = 0.5 * log(Ppluspz / Pminuspz); 
   }	//	else
   theta = atan2(pt, pz);
   phi = TVector2::Phi_0_2pi(atan2(py, px));
}

void ParticleMC::ComputeEventDependentQuantities(EventMC& event) {
   try {
      // Get the beam hadon, beam lepton and exchange boson.
      const TLorentzVector& hadron = event.GetTrack(1)->Get4Vector();
      const TLorentzVector& lepton = event.GetTrack(2)->Get4Vector();
      const TLorentzVector& boson = event.GetTrack(3)->Get4Vector();
      // Calculate z using the 4-vector definition,
      // so we don't care about frame of reference.
      z = hadron.Dot(Get4Vector()) / hadron.Dot(boson);
      // Calculate properties in the proton rest frame.
      // We want pT and angle with respect to the virtual photon,
      // so use that to define the z axis.
      TLorentzRotation toHadronRest = computeBoost(hadron, &boson);
      // Boost this particle to the proton rest frame and calculate its
      // pT and angle with respect to the virtual photon:
      TLorentzVector p = (Get4Vector() *= toHadronRest);
      thetaGamma = p.Theta();
      ptVsGamma =  p.Pt();
      // Calculate phi angle around virtual photon according
      // to the HERMES convention.
      TLorentzVector bosonPrf = (TLorentzVector(boson) *= toHadronRest);
      TLorentzVector leptonPrf = (TLorentzVector(lepton) *= toHadronRest);
      phiPrf = computeHermesPhiH(p, leptonPrf, bosonPrf);
      // Feynman x with xF = 2 * pz / W in the boson-hadron CM frame.
      // First boost to boson-hadron centre-of-mass frame.
      // Use the photon to define the z direction.
      TLorentzRotation boost = computeBoost(boson + hadron, &boson);
      xFeynman = 2. * (Get4Vector() *= boost).Pz() / sqrt(event.GetW2());
      // Determine the PDG code of the parent particle, if the particle
      // has a parent and the parent is present in the particle array.
      // The index of the particles from the Monte Carlo runs from [1,N]
      // while the index in the array runs from [0,N-1], so subtract 1
      // from the parent index to find its position.
      if(event.GetNTracks() > unsigned(orig - 1)) {
         parentId = event.GetTrack(orig - 1)->Id();
      } // if
   } // try
   catch(std::exception& e) {
      std::cerr <<
      "Exception in Particle::ComputeEventDependentQuantities: " <<
      e.what() << std::endl;
   } // catch
}

TLorentzVector ParticleMC::Get4Vector() const {
   return TLorentzVector(px, py, pz, E);
}

const EventMC* ParticleMC::GetEvent() const {
   return dynamic_cast<EventMC*>(event.GetObject());
}

const ParticleMC* ParticleMC::GetChild(UShort_t u) const {
   // Look up this particle's child via the event
   // containing it and the index of the child in that event.
   if(not GetEvent()) {
      return NULL;
   } // if
   // index is in the range [1,N]
   unsigned idx = daughter + u;
   if(daughter < 1 or // If first daughter index = 0, it has no children
      u >= GetNChildren()) { // Insufficient children
      return NULL;
   } // if
   --idx; // Convert [1,N] --> [0,N)
   const ParticleMC* p(NULL);
   // Check this index is within the # of particles in the event
   if(idx < GetEvent()->GetNTracks()) {
      p = GetEvent()->GetTrack(idx);
   } // if
   return p;
}

const ParticleMC* ParticleMC::GetParent() const {
   // Look up this particle's parent via the event
   // containing it and the index of the parent in that event.
   const ParticleMC* p(NULL);
   if(GetEvent()) {
      if(GetEvent()->GetNTracks() >= GetParentIndex()) {
         p = GetEvent()->GetTrack(GetParentIndex() - 1);
      } // if
   } // if
   return p;
}

Bool_t ParticleMC::HasChild(Int_t pdg) const {
   for(UInt_t i(0); i < GetNChildren(); ++i) {
      if(not GetChild(i)) {
         continue;
      } // if
      if(pdg == GetChild(i)->Id()) {
         return true;
      } // if
   } // for
   return false;
}

TLorentzVector
ParticleMC::Get4VectorInHadronBosonFrame() const {
   double p_ = ptVsGamma / ::sin(ptVsGamma);
   double e_ = ::sqrt(::pow(p_, 2.) + ::pow(m, 2.));
   double px_ = ptVsGamma * ::cos(phiPrf);
   double py_ = ptVsGamma * ::sin(phiPrf);
   double pz_ = ptVsGamma / ::tan(thetaGamma);
   return TLorentzVector(px_, py_, pz_, e_);
}

void ParticleMC::SetEvent(EventMC* e) {
   event = e;
}

void ParticleMC::Set4Vector(const TLorentzVector& v) {
   E = v.Energy();
   px = v.Px();
   py = v.Py();
   pz = v.Pz();
   ComputeDerivedQuantities(); // Rapidity etc
}

void ParticleMC::SetVertex(const TVector3& v) {
   xv = v.X();
   yv = v.Y();
   zv = v.Z();
}

} // namespace erhic
