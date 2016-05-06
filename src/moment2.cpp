
#include <fstream>

#include <limits>

#include <boost/numeric/ublas/lu.hpp>
#include <boost/lexical_cast.hpp>

#include "scsi/constants.h"
#include "scsi/moment2.h"

#include "scsi/rf_cavity.h"

#include "scsi/h5loader.h"


// Long. sampling frequency [Hz]; must be set to RF Cavity frequency.
# define SampleFreq   80.5e6
// Sampling distance [m].
# define SampleLambda (C0/SampleFreq*MtoMM)


namespace {
// http://www.crystalclearsoftware.com/cgi-bin/boost_wiki/wiki.pl?LU_Matrix_Inversion
// by LU-decomposition.
void inverse(Moment2ElementBase::value_t& out, const Moment2ElementBase::value_t& in)
{
    using boost::numeric::ublas::permutation_matrix;
    using boost::numeric::ublas::lu_factorize;
    using boost::numeric::ublas::lu_substitute;
    using boost::numeric::ublas::identity_matrix;

    Moment2ElementBase::value_t scratch(in); // copy
    permutation_matrix<size_t> pm(scratch.size1());
    if(lu_factorize(scratch, pm)!=0)
        throw std::runtime_error("Failed to invert matrix");
    out.assign(identity_matrix<double>(scratch.size1()));
    //out = identity_matrix<double>(scratch.size1());
    lu_substitute(scratch, pm, out);
}

} // namespace

Moment2State::Moment2State(const Config& c)
    :StateBase(c)
    ,ref()
    ,real()
    ,moment0(maxsize, 0e0)
    ,state(boost::numeric::ublas::identity_matrix<double>(maxsize))
{
    /* check to see if "cstate" is defined.
     * If "cstate" is defined then we are simulating one of many charge states.
     * If not take IonZ, moment0, and state directly from config variables.
     * If so, then
     *   for IonZ expect the config vector "IonChargeStates" and index with "cstate" (0 index).
     *   append the value of "cstate" to the vector and matrix variable names.
     *     eg. cstate=1, vector_variable=S -> looks for variable "S1".
     */
    double icstate_f = 0.0;
    bool multistate = c.tryGet<double>("cstate", icstate_f);
    size_t icstate = (size_t)icstate_f;

    std::string vectorname(c.get<std::string>("vector_variable", "moment0"));
    std::string matrixname(c.get<std::string>("matrix_variable", "initial"));

    ref.IonZ = c.get<double>("IonZ", 0e0);

    if(multistate) {
        const std::vector<double>& ics = c.get<std::vector<double> >("IonChargeStates");
        if(icstate>=ics.size())
            throw std::invalid_argument("IonChargeStates[cstate] is out of bounds");
        ref.IonZ = ics[icstate];

        std::string icstate_s(boost::lexical_cast<std::string>(icstate));
        vectorname  += icstate_s;
        matrixname  += icstate_s;
    }

    try{
        const std::vector<double>& I = c.get<std::vector<double> >(vectorname);
        if(I.size()!=moment0.size())
            throw std::invalid_argument("Initial moment0 size mis-match");
        std::copy(I.begin(), I.end(), moment0.begin());
    }catch(key_error&){
        if(multistate)
            throw std::invalid_argument(vectorname+" not defined");
        // default to zeros
    }catch(boost::bad_any_cast&){
        throw std::invalid_argument("'initial' has wrong type (must be vector)");
    }

    try{
        const std::vector<double>& I = c.get<std::vector<double> >(matrixname);
        if(I.size()!=state.size1()*state.size2())
            throw std::invalid_argument("Initial state size mis-match");
        std::copy(I.begin(), I.end(), state.data().begin());
    }catch(key_error&){
        if(multistate)
            throw std::invalid_argument(matrixname+" not defined");
        // default to identity
    }catch(boost::bad_any_cast&){
        throw std::invalid_argument("'initial' has wrong type (must be vector)");
    }

    ref.IonEs      = c.get<double>("IonEs", 0e0),
    ref.IonEk      = c.get<double>("IonEk", 0e0);

    ref.IonW       = ref.IonEs + ref.IonEk;
    ref.gamma      = (ref.IonEs != 0e0)? ref.IonW/ref.IonEs : 1e0;
    ref.beta       = sqrt(1e0-1e0/sqr(ref.gamma));
    ref.bg         = (ref.beta != 0e0)? ref.beta*ref.gamma : 1e0;

    ref.SampleIonK = (ref.IonEs != 0e0)? 2e0*M_PI/(ref.beta*SampleLambda) : 2e0*M_PI/SampleLambda;

    real           = ref;

    // Initialized by user.
//    real.phis   = moment0[PS_S];
//    real.IonEk += moment0[PS_PS]*MeVtoeV;
}

Moment2State::~Moment2State() {}

Moment2State::Moment2State(const Moment2State& o, clone_tag t)
    :StateBase(o, t)
    ,ref(o.ref)
    ,real(o.real)
    ,moment0(o.moment0)
    ,state(o.state)
{}

void Moment2State::assign(const StateBase& other)
{
    const Moment2State *O = dynamic_cast<const Moment2State*>(&other);
    if(!O)
        throw std::invalid_argument("Can't assign State: incompatible types");
    ref     = O->ref;
    real    = O->real;
    moment0 = O->moment0;
    state   = O->state;
    StateBase::assign(other);
}

void Moment2State::show(std::ostream& strm) const
{
    int j, k;

    strm << std::scientific << std::setprecision(8)
         << "\nState:\n  energy [eV] =\n" << std::setw(20) << real.IonEk << "\n  moment0 =\n    ";
    for (k = 0; k < Moment2State::maxsize; k++)
        strm << std::scientific << std::setprecision(8) << std::setw(16) << moment0(k);
    strm << "\n  state =\n";
    for (j = 0; j < Moment2State::maxsize; j++) {
        strm << "    ";
        for (k = 0; k < Moment2State::maxsize; k++) {
            strm << std::scientific << std::setprecision(8) << std::setw(16) << state(j, k);
        }
        if (j < Moment2State::maxsize-1) strm << "\n";
    }
}

bool Moment2State::getArray(unsigned idx, ArrayInfo& Info) {
    unsigned I=0;
    if(idx==I++) {
        Info.name = "state";
        Info.ptr = &state(0,0);
        Info.type = ArrayInfo::Double;
        Info.ndim = 2;
        Info.dim[0] = state.size1();
        Info.dim[1] = state.size2();
        return true;
    } else if(idx==I++) {
        Info.name = "moment0";
        Info.ptr = &moment0(0);
        Info.type = ArrayInfo::Double;
        Info.ndim = 1;
        Info.dim[0] = moment0.size();
        return true;
    } else if(idx==I++) {
        Info.name = "ref_IonZ";
        Info.ptr = &ref.IonZ;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "ref_IonEs";
        Info.ptr = &ref.IonEs;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "ref_IonW";
        Info.ptr = &ref.IonW;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "ref_gamma";
        Info.ptr = &ref.gamma;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "ref_beta";
        Info.ptr = &ref.beta;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "ref_bg";
        Info.ptr = &ref.bg;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "ref_SampleIonK";
        Info.ptr = &ref.SampleIonK;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "ref_phis";
        Info.ptr = &ref.phis;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "ref_IonEk";
        Info.ptr = &ref.IonEk;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "real_IonZ";
        Info.ptr = &real.IonZ;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "real_IonEs";
        Info.ptr = &real.IonEs;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "real_IonW";
        Info.ptr = &real.IonW;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "real_gamma";
        Info.ptr = &real.gamma;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "real_beta";
        Info.ptr = &real.beta;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "real_bg";
        Info.ptr = &real.bg;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "real_SampleIonK";
        Info.ptr = &real.SampleIonK;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "real_phis";
        Info.ptr = &real.phis;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    } else if(idx==I++) {
        Info.name = "real_IonEk";
        Info.ptr = &real.IonEk;
        Info.type = ArrayInfo::Double;
        Info.ndim = 0;
        return true;
    }
    return StateBase::getArray(idx-I, Info);
}

Moment2ElementBase::Moment2ElementBase(const Config& c)
    :ElementVoid(c)
    ,transfer(state_t::maxsize, state_t::maxsize)
    ,transfer_raw(boost::numeric::ublas::identity_matrix<double>(state_t::maxsize))
    ,misalign(boost::numeric::ublas::identity_matrix<double>(state_t::maxsize))
    ,misalign_inv(state_t::maxsize, state_t::maxsize)
    ,scratch(state_t::maxsize, state_t::maxsize)
{

    inverse(misalign_inv, misalign);

    // spoil to force recalculation of energy dependent terms
    last_Kenergy_in = last_Kenergy_out = std::numeric_limits<double>::quiet_NaN();
}

Moment2ElementBase::~Moment2ElementBase() {}

void Moment2ElementBase::assign(const ElementVoid *other)
{
    const Moment2ElementBase *O = static_cast<const Moment2ElementBase*>(other);
    length = O->length;
    transfer = O->transfer;
    transfer_raw = O->transfer_raw;
    misalign = O->misalign;
    ElementVoid::assign(other);

    // spoil to force recalculation of energy dependent terms
    last_Kenergy_in = last_Kenergy_out = std::numeric_limits<double>::quiet_NaN();
}

void Moment2ElementBase::show(std::ostream& strm) const
{
    using namespace boost::numeric::ublas;
    ElementVoid::show(strm);
    strm<<"Length "<<length<<"\n"
          "Transfer: "<<transfer<<"\n"
          "Transfer Raw: "<<transfer_raw<<"\n"
          "Mis-align: "<<misalign<<"\n";
}

void Moment2ElementBase::advance(StateBase& s)
{
    state_t& ST = static_cast<state_t&>(s);
    using namespace boost::numeric::ublas;

    // IonEk is Es + E_state; the latter is set by user.
    ST.real.IonW       = ST.real.IonEk + ST.real.IonEs;
    ST.real.gamma      = (ST.real.IonEs != 0e0)? (ST.real.IonEs+ST.real.IonEk)/ST.real.IonEs : 1e0;
    ST.real.beta       = sqrt(1e0-1e0/sqr(ST.real.gamma));
    ST.real.bg         = (ST.real.beta != 0e0)? ST.real.beta*ST.real.gamma : 1e0;
    ST.real.SampleIonK = 2e0*M_PI/(ST.real.beta*SampleLambda);

    if(ST.real.IonEk!=last_Kenergy_in) {
        // need to re-calculate energy dependent terms

        recompute_matrix(ST); // updates transfer and last_Kenergy_out

        noalias(scratch)  = prod(misalign, transfer);
        noalias(transfer) = prod(scratch, misalign_inv);

        ST.real.IonW       = ST.real.IonEk + ST.real.IonEs;
        ST.real.gamma      = (ST.real.IonEs != 0e0)? (ST.real.IonEs+ST.real.IonEk)/ST.real.IonEs : 1e0;
        ST.real.beta       = sqrt(1e0-1e0/sqr(ST.real.gamma));
        ST.real.bg         = (ST.real.beta != 0e0)? ST.real.beta*ST.real.gamma : 1e0;
        ST.real.SampleIonK = 2e0*M_PI/(ST.real.beta*SampleLambda);
    }

    // recompute_matrix only called when ST.IonEk != last_Kenergy_in.
    // Matrix elements are scaled with particle energy.

    ST.pos += length;

    std::string t_name = type_name(); // C string -> C++ string.
    if (t_name != "rfcavity") {
        ST.ref.phis   += ST.ref.SampleIonK*length*MtoMM;
        ST.real.phis  += ST.real.SampleIonK*length*MtoMM;
        ST.real.IonEk  = last_Kenergy_out;
    }

    ST.moment0 = prod(transfer, ST.moment0);

    if (t_name == "rfcavity") {
        ST.moment0[state_t::PS_S]  = ST.real.phis - ST.ref.phis;
        ST.moment0[state_t::PS_PS] = (ST.real.IonEk-ST.ref.IonEk)/MeVtoeV;
    }

    noalias(scratch) = prod(transfer, ST.state);
    noalias(ST.state) = prod(scratch, trans(transfer));
}

void Moment2ElementBase::recompute_matrix(state_t& ST)
{
    // Default, for passive elements.

    transfer = transfer_raw;

    std::string t_name = type_name(); // C string -> C++ string.
//    if (t_name != "drift") {
//        // Scale matrix elements.
//        for (unsigned k = 0; k < 2; k++) {
//            transfer(2*k, 2*k+1) *= ST.bg_ref/ST.bg1;
//            transfer(2*k+1, 2*k) *= ST.bg1/ST.bg_ref;
//        }
//    }

    last_Kenergy_in = last_Kenergy_out = ST.real.IonEk; // no energy gain
}

namespace {

void GetEdgeMatrix(const double rho, const double phi, typename Moment2ElementBase::value_t &M)
{
    typedef typename Moment2ElementBase::state_t state_t;

    M = boost::numeric::ublas::identity_matrix<double>(state_t::maxsize);

    M(state_t::PS_PX, state_t::PS_X) =  tan(phi)/rho;
    M(state_t::PS_PY, state_t::PS_Y) = -tan(phi)/rho;
}

void GetQuadMatrix(const double L, const double K, const unsigned ind, typename Moment2ElementBase::value_t &M)
{
    // 2D quadrupole transport matrix.
    double sqrtK,
           psi,
           cs,
           sn;

    if (K > 0e0) {
        // Focusing.
        sqrtK = sqrt(K);
        psi = sqrtK*L;
        cs = ::cos(psi);
        sn = ::sin(psi);

        M(ind, ind) = M(ind+1, ind+1) = cs;
        if (sqrtK != 0e0)
            M(ind, ind+1) = sn/sqrtK;
        else
            M(ind, ind+1) = L;
        if (sqrtK != 0e0)
            M(ind+1, ind) = -sqrtK*sn;
        else
            M(ind+1, ind) = 0e0;
    } else {
        // Defocusing.
        sqrtK = sqrt(-K);
        psi = sqrtK*L;
        cs = ::cosh(psi);
        sn = ::sinh(psi);

        M(ind, ind) = M(ind+1, ind+1) = cs;
        if (sqrtK != 0e0)
            M(ind, ind+1) = sn/sqrtK;
        else
            M(ind, ind+1) = L;
        if (sqrtK != 0e0)
            M(ind+1, ind) = sqrtK*sn;
        else
            M(ind+1, ind) = 0e0;
    }
}

void GetSolMatrix(const double L, const double K, typename Moment2ElementBase::value_t &M)
{
    typedef typename Moment2ElementBase::state_t state_t;

    double C = ::cos(K*L),
           S = ::sin(K*L);

    M(state_t::PS_X, state_t::PS_X)
            = M(state_t::PS_PX, state_t::PS_PX)
            = M(state_t::PS_Y, state_t::PS_Y)
            = M(state_t::PS_PY, state_t::PS_PY)
            = sqr(C);

    if (K != 0e0)
        M(state_t::PS_X, state_t::PS_PX) = S*C/K;
    else
        M(state_t::PS_X, state_t::PS_PX) = L;
    M(state_t::PS_X, state_t::PS_Y) = S*C;
    if (K != 0e0)
        M(state_t::PS_X, state_t::PS_PY) = sqr(S)/K;
    else
        M(state_t::PS_X, state_t::PS_PY) = 0e0;

    M(state_t::PS_PX, state_t::PS_X) = -K*S*C;
    M(state_t::PS_PX, state_t::PS_Y) = -K*sqr(S);
    M(state_t::PS_PX, state_t::PS_PY) = S*C;

    M(state_t::PS_Y, state_t::PS_X) = -S*C;
    if (K != 0e0)
        M(state_t::PS_Y, state_t::PS_PX) = -sqr(S)/K;
    else
        M(state_t::PS_Y, state_t::PS_PX) = 0e0;
    if (K != 0e0)
        M(state_t::PS_Y, state_t::PS_PY) = S*C/K;
    else
        M(state_t::PS_Y, state_t::PS_PY) = L;

    M(state_t::PS_PY, state_t::PS_X) = K*sqr(S);
    M(state_t::PS_PY, state_t::PS_PX) = -S*C;
    M(state_t::PS_PY, state_t::PS_Y) = -K*S*C;

    // Longitudinal plane.
    // For total path length.
//        M(state_t::PS_S, state_t::PS_S) = L;
}

struct ElementSource : public Moment2ElementBase
{
    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;
    ElementSource(const Config& c)
        :base_t(c), istate(c)
    {}

    virtual void advance(StateBase& s)
    {
        state_t& ST = static_cast<state_t&>(s);
        // Replace state with our initial values
        ST.assign(istate);
    }

    virtual void show(std::ostream& strm) const
    {
        ElementVoid::show(strm);
        strm<<"Initial: "<<istate.state<<"\n";
    }

    state_t istate;
    // note that 'transfer' is not used by this element type

    virtual ~ElementSource() {}

    virtual const char* type_name() const {return "source";}
};

struct ElementMark : public Moment2ElementBase
{
    // Transport (identity) matrix for a Marker.
    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;
    ElementMark(const Config& c) :base_t(c){
        length = 0e0;
    }
    virtual ~ElementMark() {}
    virtual const char* type_name() const {return "marker";}
};

struct ElementDrift : public Moment2ElementBase
{
    // Transport matrix for a Drift.
    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;
    ElementDrift(const Config& c)
        :base_t(c)
    {
    }
    virtual ~ElementDrift() {}

    virtual void recompute_matrix(state_t& ST)
    {
        double L = length*MtoMM; // Convert from [m] to [mm].

        this->transfer_raw(state_t::PS_X, state_t::PS_PX) = L;
        this->transfer_raw(state_t::PS_Y, state_t::PS_PY) = L;
        transfer_raw(state_t::PS_S, state_t::PS_PS) =
                -2e0*M_PI/(SampleLambda*ST.real.IonEs/MeVtoeV*cube(ST.real.bg))*L;

        transfer = transfer_raw;

        last_Kenergy_in = last_Kenergy_out = ST.real.IonEk; // no energy gain
    }

    virtual const char* type_name() const {return "drift";}
};

struct ElementSBend : public Moment2ElementBase
{
    // Transport matrix for a Gradient Sector Bend (cylindrical coordinates).

    // *** Add entrance and exit angles.

    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;
    ElementSBend(const Config& c)
        :base_t(c)
    {
    }
    virtual ~ElementSBend() {}

    virtual void recompute_matrix(state_t& ST)
    {
        double L    = conf().get<double>("L")*MtoMM,
               phi  = conf().get<double>("phi")*M_PI/180e0,
               phi1 = conf().get<double>("phi1")*M_PI/180e0,
               phi2 = conf().get<double>("phi2")*M_PI/180e0,
               rho  = L/phi,
               K    = conf().get<double>("K", 0e0)/sqr(MtoMM),
               Kx   = K + 1e0/sqr(rho),
               Ky   = -K,
               dx   = 0e0,
               sx   = 0e0;

        typename Moment2ElementBase::value_t edge1, edge2;

        // Edge focusing.
        GetEdgeMatrix(rho, phi1, edge1);
        // Horizontal plane.
        GetQuadMatrix(L, Kx, (unsigned)state_t::PS_X, this->transfer_raw);
        // Vertical plane.
        GetQuadMatrix(L, Ky, (unsigned)state_t::PS_Y, this->transfer_raw);

        // Include dispersion.
        if (Kx == 0e0) {
            dx = sqr(L)/2e0;
            sx = L;
        } else if (Kx > 0e0) {
            dx = (1e0-cos(sqrt(Kx)*L))/Kx;
            sx = sin(sqrt(Kx)*L)/sqrt(Kx);
        } else {
            dx = (1e0-cosh(sqrt(-Kx)*L))/Kx;
            sx = sin(sqrt(Kx)*L)/sqrt(Kx);
        }

        this->transfer_raw(state_t::PS_X,  state_t::PS_PS) = dx/(rho*sqr(ST.ref.beta)*ST.ref.gamma*ST.ref.IonEs/MeVtoeV);
        this->transfer_raw(state_t::PS_PX, state_t::PS_PS) = sx/(rho*sqr(ST.ref.beta)*ST.ref.gamma*ST.ref.IonEs/MeVtoeV);
        this->transfer_raw(state_t::PS_S,  state_t::PS_X)  = sx/rho*ST.ref.SampleIonK;
        this->transfer_raw(state_t::PS_S,  state_t::PS_PX) = dx/rho*ST.ref.SampleIonK;
        // Low beta approximation.
        this->transfer_raw(state_t::PS_S,  state_t::PS_PS) =
                ((L-sx)/(Kx*sqr(rho))-L/sqr(ST.ref.gamma))*ST.ref.SampleIonK
                /(sqr(ST.ref.beta)*ST.ref.gamma*ST.ref.IonEs/MeVtoeV);

        double qmrel = (ST.real.IonZ-ST.ref.IonZ)/ST.ref.IonZ;

        // Add dipole terms.
        this->transfer_raw(state_t::PS_X,  6) = -dx/rho*qmrel;
        this->transfer_raw(state_t::PS_PX, 6) = -sx/rho*qmrel;
        // Check expression.
        this->transfer_raw(state_t::PS_S,  6) =
                -((L-sx)/(Kx*sqr(rho))-L/sqr(ST.ref.gamma)+L/sqr(ST.ref.gamma))*ST.ref.SampleIonK*qmrel;

        // Edge focusing.
        GetEdgeMatrix(rho, phi2, edge2);

        transfer_raw = prod(transfer_raw, edge1);
        transfer_raw = prod(edge2, transfer_raw);

        // Longitudinal plane.
        // For total path length.
//        this->transfer_raw(state_t::PS_S,  state_t::PS_S) = L;

        transfer = transfer_raw;

        last_Kenergy_in = last_Kenergy_out = ST.real.IonEk; // no energy gain
    }

    virtual const char* type_name() const {return "sbend";}
};

struct ElementQuad : public Moment2ElementBase
{
    // Transport matrix for a Quadrupole; K = B2/Brho.
    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;
    ElementQuad(const Config& c)
        :base_t(c)
    {
    }
    virtual ~ElementQuad() {}

    virtual void recompute_matrix(state_t& ST)
    {
        // Re-initialize transport matrix.
        this->transfer_raw = boost::numeric::ublas::identity_matrix<double>(state_t::maxsize);

        double Brho = ST.real.beta*(ST.real.IonEk+ST.real.IonEs)/(C0*ST.real.IonZ),
               K    = conf().get<double>("B2")/Brho/sqr(MtoMM),
               L    = conf().get<double>("L")*MtoMM;

        // Horizontal plane.
        GetQuadMatrix(L,  K, (unsigned)state_t::PS_X, this->transfer_raw);
        // Vertical plane.
        GetQuadMatrix(L, -K, (unsigned)state_t::PS_Y, this->transfer_raw);
        // Longitudinal plane.
//        this->transfer_raw(state_t::PS_S, state_t::PS_S) = L;

        transfer_raw(state_t::PS_S, state_t::PS_PS) =
                -2e0*M_PI/(SampleLambda*ST.real.IonEs/MeVtoeV*cube(ST.real.bg))*L;

        transfer = transfer_raw;

        last_Kenergy_in = last_Kenergy_out = ST.real.IonEk; // no energy gain
    }

    virtual const char* type_name() const {return "quadrupole";}
};

struct ElementSolenoid : public Moment2ElementBase
{
    // Transport (identity) matrix for a Solenoid; K = B0/(2 Brho).
    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;
    ElementSolenoid(const Config& c)
        :base_t(c)
    {
    }
    virtual ~ElementSolenoid() {}
    virtual void recompute_matrix(state_t& ST)
    {
        // Re-initialize transport matrix.
        this->transfer_raw = boost::numeric::ublas::identity_matrix<double>(state_t::maxsize);

        double Brho = ST.real.beta*(ST.real.IonEk+ST.real.IonEs)/(C0*ST.real.IonZ),
               K    = conf().get<double>("B")/(2e0*Brho)/MtoMM,
               L    = conf().get<double>("L")*MtoMM;      // Convert from [m] to [mm].

        GetSolMatrix(L, K, this->transfer_raw);

        transfer_raw(state_t::PS_S, state_t::PS_PS) =
                -2e0*M_PI/(SampleLambda*ST.real.IonEs/MeVtoeV*cube(ST.real.bg))*L;

        transfer = transfer_raw;

        last_Kenergy_in = last_Kenergy_out = ST.real.IonEk; // no energy gain
    }

    virtual const char* type_name() const {return "solenoid";}
};


struct ElementRFCavity : public Moment2ElementBase
{
    // Transport matrix for an RF Cavity.
    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;

    CavDataType    CavData;
    std::fstream   inf;
    CavTLMLineType CavTLMLineTab;

    ElementRFCavity(const Config& c)
        :base_t(c)
    {
        std::string cav_type = c.get<std::string>("cavtype");
        double L             = c.get<double>("L")*MtoMM;         // Convert from [m] to [mm].

        std::string CavType      = conf().get<std::string>("cavtype");
        std::string Eng_Data_Dir = conf().get<std::string>("Eng_Data_Dir", "");

        if (CavType == "0.041QWR") {
            CavData.RdData(Eng_Data_Dir+"/axisData_41.txt");
            inf.open((Eng_Data_Dir+"/Multipole41/thinlenlon_41.txt").c_str(), std::ifstream::in);
        } else if (conf().get<std::string>("cavtype") == "0.085QWR") {
            CavData.RdData(Eng_Data_Dir+"/axisData_85.txt");
            inf.open((Eng_Data_Dir+"/Multipole85/thinlenlon_85.txt").c_str(), std::ifstream::in);
        } else {
            std::ostringstream strm;
            strm << "*** InitRFCav: undef. cavity type: " << CavType << "\n";
            throw std::runtime_error(strm.str());
        }

        this->transfer_raw(state_t::PS_X, state_t::PS_PX) = L;
        this->transfer_raw(state_t::PS_Y, state_t::PS_PY) = L;
        // For total path length.
//        this->transfer(state_t::PS_S, state_t::PS_S)  = L;
    }

    void GetCavMatParams(const int cavi,
                         const double beta_tab[], const double gamma_tab[], const double IonK[]);

    typedef boost::numeric::ublas::matrix<double> value_mat;

    void GetCavMat(const int cavi, const int cavilabel, const double Rm, Particle &real,
                   const double EfieldScl, const double IonFyi_s,
                   const double IonEk_s, const double fRF, value_mat &M);

    void GenCavMat(const int cavi, const double dis, const double EfieldScl, const double TTF_tab[],
                   const double beta_tab[], const double gamma_tab[], const double Lambda,
                   Particle &real, const double IonFys[], const double Rm, value_mat &M);

    void PropagateLongRFCav(Config &conf, Particle &ref);

    void InitRFCav(const Config &conf, Particle &real, double &accIonW,
                   double &avebeta, double &avegamma, value_mat &M);

    void GetCavBoost(const CavDataType &CavData, Particle &state, const double IonFy0, const double fRF,
                     const double EfieldScl, double &IonFy, double &accIonW);

    virtual ~ElementRFCavity() {}

    virtual void recompute_matrix(state_t& ST)
    {
        transfer = transfer_raw;

        last_Kenergy_in = ST.real.IonEk;

        this->ElementRFCavity::PropagateLongRFCav(conf(), ST.ref);

        last_Kenergy_out = ST.real.IonEk;

        // Define initial conditions.
        double accIonW, avebeta, avegamma;

        this->ElementRFCavity::InitRFCav(conf(), ST.real, accIonW, avebeta, avegamma, transfer);
   }

    virtual const char* type_name() const {return "rfcavity";}
};


void ElementRFCavity::GetCavMatParams(const int cavi, const double beta_tab[], const double gamma_tab[], const double IonK[])
{
    // Evaluate time transit factors and acceleration.

    std::string       line, Elem, Name;
    double            s, Length, Aper, E0, T, S, Accel;
    std::stringstream str;

    inf.clear();
    inf.seekg(0, inf.beg);

    CavTLMLineTab.clear();

    s = CavData.s[0];
    while (getline(inf, line) && !inf.fail()) {
        T = 0e0, S = 0e0, Accel = 0e0;
        if (line[0] == '%') {
            // Comment.
        } else {
            str.str(line);
            str.clear();
            str >> Elem >> Name >> Length >> Aper;

            s += Length;

            if ((Elem != "drift") && (Elem != "AccGap"))
                str >> E0;
            else
                E0 = 0e0;

            if (Elem == "drift") {
            } else if (Elem == "EFocus1") {
                if (s < 0e0) {
                    // First gap. By reflection 1st Gap EFocus1 is 2nd gap EFocus2.
                    TransitFacMultipole(cavi, "CaviMlp_EFocus2", IonK[0], T, S);
                    // First gap *1, transverse E field the same.
                    S = -S;
                } else {
                    // Second gap.
                    TransitFacMultipole(cavi, "CaviMlp_EFocus1", IonK[1], T, S);
                }
            } else if (Elem == "EFocus2") {
                if (s < 0e0) {
                    // First gap.
                    TransitFacMultipole(cavi, "CaviMlp_EFocus1", IonK[0], T, S);
                    S = -S;
                } else {
                    // Second gap.
                    TransitFacMultipole(cavi, "CaviMlp_EFocus2", IonK[1], T, S);
                }
            } else if (Elem == "EDipole") {
                if (MpoleLevel >= 1) {
                    if (s < 0e0) {
                        TransitFacMultipole(cavi, "CaviMlp_EDipole", IonK[0], T, S);
                        // First gap *1, transverse E field the same.
                        S = -S;
                    } else {
                        // Second gap.
                        TransitFacMultipole(cavi, "CaviMlp_EDipole", IonK[1], T, S);
                    }
                }
            } else if (Elem == "EQuad") {
                if (MpoleLevel >= 2) {
                    if (s < 0e0) {
                        // First gap.
                        TransitFacMultipole(cavi, "CaviMlp_EQuad", IonK[0], T, S);
                        S = -S;
                    } else {
                        // Second Gap
                        TransitFacMultipole(cavi, "CaviMlp_EQuad", IonK[1], T, S);
                    }
                }
            } else if (Elem == "HMono") {
                if (MpoleLevel >= 2) {
                    if (s < 0e0) {
                        // First gap.
                        TransitFacMultipole(cavi, "CaviMlp_HMono", IonK[0], T, S);
                        T = -T;
                    } else {
                        // Second Gap
                        TransitFacMultipole(cavi, "CaviMlp_HMono", IonK[1], T, S);
                    }
                }
            } else if (Elem == "HDipole") {
                if (MpoleLevel >= 1) {
                    if (s < 0e0) {
                        // First gap.
                        TransitFacMultipole(cavi, "CaviMlp_HDipole", IonK[0], T, S);
                        T = -T;
                    }  else {
                        // Second gap.
                        TransitFacMultipole(cavi, "CaviMlp_HDipole", IonK[1], T, S);
                    }
                }
            } else if (Elem == "HQuad") {
                if (MpoleLevel >= 2) {
                    if (s < 0e0) {
                        // First gap.
                        TransitFacMultipole(cavi, "CaviMlp_HQuad", IonK[0], T, S);
                        T = -T;
                    } else {
                        // Second gap.
                        TransitFacMultipole(cavi, "CaviMlp_HQuad", IonK[1], T, S);
                    }
                }
            } else if (Elem == "AccGap") {
                if (s < 0e0) {
                    // First gap.
                    Accel = (beta_tab[0]*gamma_tab[0])/((beta_tab[1]*gamma_tab[1]));
                } else {
                    // Second gap.
                    Accel = (beta_tab[1]*gamma_tab[1])/((beta_tab[2]*gamma_tab[2]));
                }
            } else {
                std::ostringstream strm;
                strm << "*** GetCavMatParams: undef. multipole element " << Elem << "\n";
                throw std::runtime_error(strm.str());
            }

            CavTLMLineTab.set(s, Elem, E0, T, S, Accel);
        }
    }

    if (false) {
        std::cout << "\n";
        CavTLMLineTab.show(std::cout);
    }
}


void ElementRFCavity::GenCavMat(const int cavi, const double dis, const double EfieldScl, const double TTF_tab[],
                                const double beta_tab[], const double gamma_tab[], const double Lambda,
                                Particle &real, const double IonFys[], const double Rm, value_mat &M)
{
    /* RF cavity model, transverse only defocusing.
     * 2-gap matrix model.                                            */

    std::string       line, Elem, Name;
    int               seg, n;
    double            Length, Aper, Efield, s, k_s[3];
    double            Ecens[2], Ts[2], Ss[2], V0s[2], ks[2], L1, L2, L3;
    double            beta, gamma, kfac, V0, T, S, kfdx, kfdy, dpy, Accel, IonFy;
    value_mat         Idmat, Mlon_L1, Mlon_K1, Mlon_L2;
    value_mat         Mlon_K2, Mlon_L3, Mlon, Mtrans, Mprob;
    std::stringstream str;

    const double IonA = 1e0;

    using boost::numeric::ublas::prod;

    Idmat = boost::numeric::ublas::identity_matrix<double>(PS_Dim);

    inf.clear();
    inf.seekg(0, inf.beg);

    k_s[0] = 2e0*M_PI/(beta_tab[0]*Lambda);
    k_s[1] = 2e0*M_PI/(beta_tab[1]*Lambda);
    k_s[2] = 2e0*M_PI/(beta_tab[2]*Lambda);

    // Longitudinal model:  Drift-Kick-Drift, dis: total lenghth centered at 0,
    // Ecens[0] & Ecens[1]: Electric Center position where accel kick applies, Ecens[0] < 0
    // TTFtab:              2*6 vector, Ecens, T Tp S Sp, V0;

    Ecens[0] = TTF_tab[0];
    Ts[0]    = TTF_tab[1];
    Ss[0]    = TTF_tab[3];
    V0s[0]   = TTF_tab[5];
    ks[0]    = 0.5*(k_s[0]+k_s[1]);
    L1       = dis + Ecens[0];       //try change dis/2 to dis 14/12/12

    Mlon_L1 = Idmat;
    Mlon_K1 = Idmat;
    // Pay attention, original is -
    Mlon_L1(4, 5) = -2e0*M_PI/Lambda*(1e0/cube(beta_tab[0]*gamma_tab[0])*MeVtoeV/real.IonEs*L1);
    // Pay attention, original is -k1-k2
    Mlon_K1(5, 4) = -real.IonZ*V0s[0]*Ts[0]*sin(IonFys[0]+ks[0]*L1)-real.IonZ*V0s[0]*Ss[0]*cos(IonFys[0]+ks[0]*L1);

    Ecens[1] = TTF_tab[6];
    Ts[1]    = TTF_tab[7];
    Ss[1]    = TTF_tab[9];
    V0s[1]   = TTF_tab[11];
    ks[1]    = 0.5*(k_s[1]+k_s[2]);
    L2       = Ecens[1] - Ecens[0];

    Mlon_L2 = Idmat;
    Mlon_K2 = Idmat;

    Mlon_L2(4, 5) = -2e0*M_PI/Lambda*(1e0/cube(beta_tab[1]*gamma_tab[1])*MeVtoeV/real.IonEs*L2); //Problem is Here!!
    Mlon_K2(5, 4) = -real.IonZ*V0s[1]*Ts[1]*sin(IonFys[1]+ks[1]*Ecens[1])-real.IonZ*V0s[1]*Ss[1]*cos(IonFys[1]+ks[1]*Ecens[1]);

    L3 = dis - Ecens[1]; //try change dis/2 to dis 14/12/12

    Mlon_L3       = Idmat;
    Mlon_L3(4, 5) = -2e0*M_PI/Lambda*(1e0/cube(beta_tab[2]*gamma_tab[2])*MeVtoeV/real.IonEs*L3);

    Mlon = Idmat;
    Mlon = prod(Mlon_K1, Mlon_L1);
    Mlon = prod(Mlon_L2, Mlon);
    Mlon = prod(Mlon_K2, Mlon);
    Mlon = prod(Mlon_L3, Mlon);

    // Transverse model
    // Drift-FD-Drift-LongiKick-Drift-FD-Drift-0-Drift-FD-Drift-LongiKick-Drift-FD-Drift

    seg    = 0;

    Mtrans = Idmat;
    Mprob  = Idmat;

    beta   = beta_tab[0];
    gamma  = gamma_tab[0];
    IonFy  = IonFys[0];
    kfac   = k_s[0];

    V0 = 0e0, T = 0e0, S = 0e0, kfdx = 0e0, kfdy = 0e0, dpy = 0e0;

    s = CavData.s[0];
    n = 0;
    while (getline(inf, line) && !inf.fail()) {
        if (line[0] == '%') {
            // Comment.
        } else {
            n++;
            str.str(line);
            str.clear();
            str >> Elem >> Name >> Length >> Aper;

            s += Length;

            if ((Elem != "drift") && (Elem != "AccGap"))
                str >> Efield;
            else
                Efield = 0e0;

            if (false)
                printf("%9.5f %8s %8s %9.5f %9.5f %9.5f\n",
                       s, Elem.c_str(), Name.c_str(), Length, Aper, Efield);

            Mprob = Idmat;
            if (Elem == "drift") {
                IonFy = IonFy + kfac*Length;

                Mprob(0, 1) = Length;
                Mprob(2, 3) = Length;
                Mtrans      = prod(Mprob, Mtrans);
            } else if (Elem == "EFocus1") {
                V0   = CavTLMLineTab.E0[n-1]*EfieldScl;
                T    = CavTLMLineTab.T[n-1];
                S    = CavTLMLineTab.S[n-1];
                kfdx = real.IonZ*V0/sqr(beta)/gamma/IonA/AU*(T*cos(IonFy)-S*sin(IonFy))/Rm;
                kfdy = kfdx;

                Mprob(1, 0) = kfdx;
                Mprob(3, 2) = kfdy;
                Mtrans      = prod(Mprob, Mtrans);
            } else if (Elem == "EFocus2") {
                V0   = CavTLMLineTab.E0[n-1]*EfieldScl;
                T    = CavTLMLineTab.T[n-1];
                S    = CavTLMLineTab.S[n-1];
                kfdx = real.IonZ*V0/sqr(beta)/gamma/IonA/AU*(T*cos(IonFy)-S*sin(IonFy))/Rm;
                kfdy = kfdx;

                Mprob(1, 0) = kfdx;
                Mprob(3, 2) = kfdy;
                Mtrans      = prod(Mprob, Mtrans);
            } else if (Elem == "EDipole") {
                if (MpoleLevel >= 1) {
                    V0  = CavTLMLineTab.E0[n-1]*EfieldScl;
                    T   = CavTLMLineTab.T[n-1];
                    S   = CavTLMLineTab.S[n-1];
                    dpy = real.IonZ*V0/sqr(beta)/gamma/IonA/AU*(T*cos(IonFy)-S*sin(IonFy));

                    Mprob(3, 6) = dpy;
                    Mtrans      = prod(Mprob, Mtrans);
                }
            } else if (Elem == "EQuad") {
                if (MpoleLevel >= 2) {
                    V0   = CavTLMLineTab.E0[n-1]*EfieldScl;
                    T    = CavTLMLineTab.T[n-1];
                    S    = CavTLMLineTab.S[n-1];
                    kfdx =  real.IonZ*V0/sqr(beta)/gamma/IonA/AU*(T*cos(IonFy)-S*sin(IonFy))/Rm;
                    kfdy = -kfdx;

                    Mprob(1, 0) = kfdx;
                    Mprob(3, 2) = kfdy;
                    Mtrans      = prod(Mprob, Mtrans);
                }
            } else if (Elem == "HMono") {
                if (MpoleLevel >= 2) {
                    V0   = CavTLMLineTab.E0[n-1]*EfieldScl;
                    T    = CavTLMLineTab.T[n-1];
                    S    = CavTLMLineTab.S[n-1];
                    kfdx = -MU0*C0*real.IonZ*V0/beta/gamma/IonA/AU*(T*cos(IonFy+M_PI/2e0)-S*sin(IonFy+M_PI/2e0))/Rm;
                    kfdy = kfdx;

                    Mprob(1, 0) = kfdx;
                    Mprob(3, 2) = kfdy;
                    Mtrans      = prod(Mprob, Mtrans);
                }
            } else if (Elem == "HDipole") {
                if (MpoleLevel >= 1) {
                    V0  = CavTLMLineTab.E0[n-1]*EfieldScl;
                    T   = CavTLMLineTab.T[n-1];
                    S   = CavTLMLineTab.S[n-1];
                    dpy = -MU0*C0*real.IonZ*V0/beta/gamma/IonA/AU*(T*cos(IonFy+M_PI/2e0)-S*sin(IonFy+M_PI/2e0));

                    Mprob(3, 6) = dpy;
                    Mtrans      = prod(Mprob, Mtrans);
                }
            } else if (Elem == "HQuad") {
                if (MpoleLevel >= 2) {
                    if (s < 0e0) {
                        // First gap.
                        beta  = (beta_tab[0]+beta_tab[1])/2e0;
                        gamma = (gamma_tab[0]+gamma_tab[1])/2e0;
                    } else {
                        beta  = (beta_tab[1]+beta_tab[2])/2e0;
                        gamma = (gamma_tab[1]+gamma_tab[2])/2e0;
                    }
                    V0   = CavTLMLineTab.E0[n-1]*EfieldScl;
                    T    = CavTLMLineTab.T[n-1];
                    S    = CavTLMLineTab.S[n-1];
                    kfdx = -MU0*C0*real.IonZ*V0/beta/gamma/IonA/AU*(T*cos(IonFy+M_PI/2e0)-S*sin(IonFy+M_PI/2e0))/Rm;
                    kfdy = -kfdx;

                    Mprob(1, 0) = kfdx;
                    Mprob(3, 2) = kfdy;
                    Mtrans      = prod(Mprob, Mtrans);
                }
            } else if (Elem == "AccGap") {
                //IonFy = IonFy + real.IonZ*V0s[0]*kfac*(TTF_tab[2]*sin(IonFy)
                //        + TTF_tab[4]*cos(IonFy))/2/((gamma-1)*real.IonEs/MeVtoeV); //TTF_tab[2]~Tp
                seg    = seg + 1;
                beta   = beta_tab[seg];
                gamma  = gamma_tab[seg];
                kfac   = 2e0*M_PI/(beta*Lambda);
                Accel  = CavTLMLineTab.Accel[n-1];

                Mprob(1, 1) = Accel;
                Mprob(3, 3) = Accel;
                Mtrans      = prod(Mprob, Mtrans);
            } else {
                std::ostringstream strm;
                strm << "*** GetCavMat: undef. multipole type " << Elem << "\n";
                throw std::runtime_error(strm.str());
            }
//            std::cout << Elem << "\n";
//            PrtMat(Mprob);
        }
    }

//    inf.close();

    M = Mtrans;

    M(4, 4) = Mlon(4, 4);
    M(4, 5) = Mlon(4, 5);
    M(5, 4) = Mlon(5, 4);
    M(5, 5) = Mlon(5, 5);
}


void ElementRFCavity::GetCavMat(const int cavi, const int cavilabel, const double Rm, Particle &real,
                                const double EfieldScl, const double IonFyi_s,
                                const double IonEk_s, const double fRF, value_mat &M)
{
    int    n;
    double IonLambda, Ecen[2], T[2], Tp[2], S[2], Sp[2], V0[2];
    double dis, IonW_s[3], IonFy_s[3], gamma_s[3], beta_s[3], IonK_s[3];
    double IonK[2];

    IonLambda  = C0/fRF*MtoMM;

    IonW_s[0]  = IonEk_s + real.IonEs;
    IonFy_s[0] = IonFyi_s;
    gamma_s[0] = IonW_s[0]/real.IonEs;
    beta_s[0]  = sqrt(1e0-1e0/sqr(gamma_s[0]));
    IonK_s[0]  = 2e0*M_PI/(beta_s[0]*IonLambda);

    n   = CavData.s.size();
    dis = (CavData.s[n-1]-CavData.s[0])/2e0;

    TransFacts(cavilabel, beta_s[0], 1, EfieldScl, Ecen[0], T[0], Tp[0], S[0], Sp[0], V0[0]);
    EvalGapModel(dis, IonW_s[0], real, IonFy_s[0], IonK_s[0], IonLambda,
                Ecen[0], T[0], S[0], Tp[0], Sp[0], V0[0], IonW_s[1], IonFy_s[1]);
    gamma_s[1] = IonW_s[1]/real.IonEs;
    beta_s[1]  = sqrt(1e0-1e0/sqr(gamma_s[1]));
    IonK_s[1]  = 2e0*M_PI/(beta_s[1]*IonLambda);

    TransFacts(cavilabel, beta_s[1], 2, EfieldScl, Ecen[1], T[1], Tp[1], S[1], Sp[1], V0[1]);
    EvalGapModel(dis, IonW_s[1], real, IonFy_s[1], IonK_s[1], IonLambda,
                Ecen[1], T[1], S[1], Tp[1], Sp[1], V0[1], IonW_s[2], IonFy_s[2]);
    gamma_s[2] = IonW_s[2]/real.IonEs;
    beta_s[2]  = sqrt(1e0-1e0/sqr(gamma_s[2]));
    IonK_s[2]  = 2e0*M_PI/(beta_s[2]*IonLambda);

    Ecen[0] = Ecen[0] - dis;

    double TTF_tab[] = {Ecen[0], T[0], Tp[0], S[0], Sp[0], V0[0], Ecen[1], T[1], Tp[1], S[1], Sp[1], V0[1]};
    IonK[0] = (IonK_s[0]+IonK_s[1])/2e0;
    IonK[1] = (IonK_s[1]+IonK_s[2])/2e0;

    if (false) {
        printf("\n GetCavMat:\n");
        printf("IonK  : %15.10f %15.10f %15.10f\n", IonK_s[0], IonK_s[1], IonK_s[2]);
        printf("IonK  : %15.10f %15.10f\n", IonK[0], IonK[1]);
        printf("beta  : %15.10f %15.10f %15.10f\n", beta_s[0], beta_s[1], beta_s[2]);
        printf("gamma : %15.10f %15.10f %15.10f\n", gamma_s[0], gamma_s[1], gamma_s[2]);
        printf("Ecen  : %15.10f %15.10f\n", Ecen[0], Ecen[1]);
        printf("T     : %15.10f %15.10f\n", T[0], T[1]);
        printf("Tp    : %15.10f %15.10f\n", Tp[0], Tp[1]);
        printf("S     : %15.10f %15.10f\n", S[0], S[1]);
        printf("Sp    : %15.10f %15.10f\n", Sp[0], Sp[1]);
        printf("V0    : %15.10f %15.10f\n", V0[0], V0[1]);
    }

    this->ElementRFCavity::GetCavMatParams(cavi, beta_s, gamma_s, IonK);
    this->ElementRFCavity::GenCavMat(cavi, dis, EfieldScl, TTF_tab, beta_s, gamma_s, IonLambda, real, IonFy_s, Rm, M);
}


void ElementRFCavity::GetCavBoost(const CavDataType &CavData, Particle &state, const double IonFy0, const double fRF,
                                  const double EfieldScl, double &IonFy, double &accIonW)
{
    int    n = CavData.s.size(),
           k;

    double dis = CavData.s[n-1] - CavData.s[0],
           dz  = dis/(n-1),
           IonW0, IonLambda, IonK, IonFylast, IonGamma, IonBeta;

    IonLambda = C0/fRF*MtoMM;


    IonW0 = state.IonW;
    IonFy = IonFy0;
    IonK  = state.SampleIonK;
    for (k = 0; k < n-1; k++) {
        IonFylast = IonFy;
        IonFy += IonK*dz;
        state.IonW  += state.IonZ*EfieldScl*(CavData.Elong[k]+CavData.Elong[k+1])/2e0
                       *cos((IonFylast+IonFy)/2e0)*dz/MtoMM;
        IonGamma = state.IonW/state.IonEs;
        IonBeta  = sqrt(1e0-1e0/sqr(IonGamma));
        if ((state.IonW-state.IonEs) < 0e0) {
            state.IonW = state.IonEs;
            IonBeta = 0e0;
        }
        IonK = 2e0*M_PI/(IonBeta*IonLambda);
    }

    accIonW = state.IonW - IonW0;
}


void ElementRFCavity::PropagateLongRFCav(Config &conf, Particle &ref)
{
    std::string CavType;
    int         cavi;
    double      fRF, multip, IonFys, EfieldScl, caviFy, IonFy_i, IonFy_o, accIonW;

    CavType = conf.get<std::string>("cavtype");
    if (CavType == "0.041QWR") {
        cavi = 1;
    } else if (conf.get<std::string>("cavtype") == "0.085QWR") {
        cavi = 2;
    } else {
        std::ostringstream strm;
        strm << "*** PropagateLongRFCav: undef. cavity type: " << CavType << "\n";
        throw std::runtime_error(strm.str());
    }

    fRF       = conf.get<double>("f");
    multip    = fRF/SampleFreq;
    IonFys    = conf.get<double>("phi")*M_PI/180e0;  // Synchrotron phase [rad].
    EfieldScl = conf.get<double>("scl_fac");         // Electric field scale factor.

    caviFy = GetCavPhase(cavi, ref, IonFys, multip);

    IonFy_i = multip*ref.phis + caviFy;
    conf.set<double>("phi_ref", caviFy);

    // For the reference particle, evaluate the change of:
    // kinetic energy, absolute phase, beta, and gamma.
    this->GetCavBoost(CavData, ref, IonFy_i, fRF, EfieldScl, IonFy_o, accIonW);

    ref.gamma       = ref.IonW/ref.IonEs;
    ref.beta        = sqrt(1e0-1e0/sqr(ref.gamma));
    ref.SampleIonK  = 2e0*M_PI/(ref.beta*SampleLambda);
    ref.phis       += (IonFy_o-IonFy_i)/multip;
    ref.IonEk       = ref.IonW - ref.IonEs;
    ref.gamma       = (ref.IonEk+ref.IonEs)/ref.IonEs;
    ref.beta        = sqrt(1e0-1e0/sqr(ref.gamma));
    ref.bg          = ref.beta*ref.gamma;
}


void ElementRFCavity::InitRFCav(const Config &conf, Particle &real, double &accIonW,
                                double &avebeta, double &avegamma, value_mat &M)
{
    std::string CavType;
    int         cavi, cavilabel, multip;
    double      Rm, IonFy_i, Ek_i, fRF, EfieldScl, IonFy_o;

    CavType = conf.get<std::string>("cavtype");
    if (CavType == "0.041QWR") {
        cavi       = 1;
        cavilabel  = 41;
        multip     = 1;
        Rm         = 17e0;
    } else if (conf.get<std::string>("cavtype") == "0.085QWR") {
        cavi       = 2;
        cavilabel  = 85;
        multip     = 1;
        Rm         = 17e0;
    } else if (conf.get<std::string>("cavtype") == "0.29HWR") {
        cavi       = 3;
        cavilabel  = 29;
        multip     = 4;
        Rm         = 20e0;
    } else if (conf.get<std::string>("cavtype") == "0.53HWR") {
        cavi       = 4;
        cavilabel  = 53;
        multip     = 4;
        Rm         = 20e0;
    } else if (conf.get<std::string>("cavtype") == "??EL") {
        // 5 Cell elliptical.
        cavi       = 5;
        cavilabel  = 53;
        multip     = 8;
        Rm         = 20e0;
    } else {
        std::ostringstream strm;
        strm << "*** InitRFCav: undef. cavity type: " << CavType << "\n";
        throw std::runtime_error(strm.str());
    }

    IonFy_i   = multip*real.phis + conf.get<double>("phi_ref");
    Ek_i      = real.IonEk;
    real.IonW = real.IonEk + real.IonEs;

    avebeta   = real.beta;
    avegamma  = real.gamma;
    fRF       = conf.get<double>("f");
    EfieldScl = conf.get<double>("scl_fac");         // Electric field scale factor.

//    J.B.: Note, this was passed:
//    CaviIonK  = 2e0*M_PI*fRF/(real.beta*C0*MtoMM);
//    vs.:
//    double SampleIonK = 2e0*M_PI/(real.beta*C0/SampleFreq*MtoMM);

    ElementRFCavity::GetCavBoost(CavData, real, IonFy_i, fRF, EfieldScl, IonFy_o, accIonW);

    real.IonEk       = real.IonW - real.IonEs;
    real.gamma       = real.IonW/real.IonEs;
    real.beta        = sqrt(1e0-1e0/sqr(real.gamma));
    real.SampleIonK  = 2e0*M_PI/(real.beta*SampleLambda);
    real.phis       += (IonFy_o-IonFy_i)/multip;
    avebeta         += real.beta;
    avebeta         /= 2e0;
    avegamma        += real.gamma;
    avegamma        /= 2e0;

    this->GetCavMat(cavi, cavilabel, Rm, real, EfieldScl, IonFy_i, Ek_i, fRF, M);
}


struct ElementStripper : public Moment2ElementBase
{
    // Transport (identity) matrix for a Charge Stripper.
    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;
    ElementStripper(const Config& c)
        :base_t(c)
    {
        // Identity matrix.
    }
    virtual ~ElementStripper() {}

    virtual const char* type_name() const {return "stripper";}
};

struct ElementEDipole : public Moment2ElementBase
{
    // Transport matrix for an Electric Dipole.
    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;
    ElementEDipole(const Config& c)
        :base_t(c)
    {
        //double L = c.get<double>("L")*MtoMM;

    }
    virtual ~ElementEDipole() {}

    virtual const char* type_name() const {return "edipole";}
};

struct ElementGeneric : public Moment2ElementBase
{
    typedef Moment2ElementBase base_t;
    typedef typename base_t::state_t state_t;
    ElementGeneric(const Config& c)
        :base_t(c)
    {
        std::vector<double> I = c.get<std::vector<double> >("transfer");
        if(I.size()>this->transfer_raw.data().size())
            throw std::invalid_argument("Initial transfer size too big");
        std::copy(I.begin(), I.end(), this->transfer_raw.data().begin());
    }
    virtual ~ElementGeneric() {}

    virtual const char* type_name() const {return "generic";}
};

} // namespace

void registerMoment2()
{
    Machine::registerState<Moment2State>("MomentMatrix2");

    Machine::registerElement<ElementSource                 >("MomentMatrix2",   "source");

    Machine::registerElement<ElementMark                   >("MomentMatrix2",   "marker");

    Machine::registerElement<ElementDrift                  >("MomentMatrix2",   "drift");

    Machine::registerElement<ElementSBend                  >("MomentMatrix2",   "sbend");

    Machine::registerElement<ElementQuad                   >("MomentMatrix2",   "quadrupole");

    Machine::registerElement<ElementSolenoid               >("MomentMatrix2",   "solenoid");

    Machine::registerElement<ElementRFCavity               >("MomentMatrix2",   "rfcavity");

    Machine::registerElement<ElementStripper               >("MomentMatrix2",   "stripper");

    Machine::registerElement<ElementEDipole                >("MomentMatrix2",   "edipole");

    Machine::registerElement<ElementGeneric                >("MomentMatrix2",   "generic");
}
