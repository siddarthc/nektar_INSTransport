///////////////////////////////////////////////////////////////////////////////
//
// File VelocityCorrection.cpp
//
// For more information, please see: http://www.nektar.info
//
// The MIT License
//
// Copyright (c) 2006 Division of Applied Mathematics, Brown University (USA),
// Department of Aeronautics, Imperial College London (UK), and Scientific
// Computing and Imaging Institute, University of Utah (USA).
//
// License for the specific language governing rights and limitations under
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Description: Velocity Correction Scheme for the Incompressible
// Navier Stokes equations
///////////////////////////////////////////////////////////////////////////////

#include <IncNavierStokesSolver/EquationSystems/VelocityCorrectionScheme.h>
#include <LibUtilities/BasicUtils/Timer.h>
#include <SolverUtils/Core/Misc.h>

#include <boost/algorithm/string.hpp>

using namespace std;

namespace Nektar
{
    string VelocityCorrectionScheme::className = 
        SolverUtils::GetEquationSystemFactory().RegisterCreatorFunction(
            "VelocityCorrectionScheme", 
            VelocityCorrectionScheme::create);

    /**
     * Constructor. Creates ...
     *
     * \param 
     * \param
     */
    VelocityCorrectionScheme::VelocityCorrectionScheme(
            const LibUtilities::SessionReaderSharedPtr& pSession)
        : UnsteadySystem(pSession),
          IncNavierStokes(pSession),
          m_varCoeffLap(StdRegions::NullVarCoeffMap)
    {
        
    }

    void VelocityCorrectionScheme::v_InitObject()
    {
        int n;
        
        IncNavierStokes::v_InitObject();
        m_explicitDiffusion = false;

        // Set m_pressure to point to last field of m_fields;
        if (boost::iequals(m_session->GetVariable(m_fields.num_elements()-1), "p"))
        {
            m_nConvectiveFields = m_fields.num_elements()-1;
            m_pressure = m_fields[m_nConvectiveFields];
        }
        else
        {
            ASSERTL0(false,"Need to set up pressure field definition");
        }

         // Determine equations on which SVV is activated
         m_useSpecVanVisc = Array<OneD, bool> (m_nConvectiveFields, false);
         m_sVVCutoffRatio = Array<OneD, NekDouble> (m_nConvectiveFields, 0.75);
         m_sVVDiffCoeff = Array<OneD, NekDouble> (m_nConvectiveFields, 0.1);
         m_useHomo1DSpecVanVisc = Array<OneD, bool> (m_nConvectiveFields, false);
  
        // Determine diffusion coefficients for each field
        m_diffCoeff = Array<OneD, NekDouble> (m_nConvectiveFields, m_kinvis);
        for (n = 0; n < m_nConvectiveFields; ++n)
        {
            std::string varName = m_session->GetVariable(n);
            if ( m_session->DefinesFunction("DiffusionCoefficient", varName))
            {
                LibUtilities::EquationSharedPtr ffunc
                    = m_session->GetFunction("DiffusionCoefficient", varName);
                m_diffCoeff[n] = ffunc->Evaluate();
            }

            if (m_session->DefinesFunction("SVVCutoffRatio", varName))
            {
                m_useSpecVanVisc[n] = true;
                LibUtilities::EquationSharedPtr ffunc
                    = m_session->GetFunction("SVVcutoffRatio", varName);
                m_sVVCutoffRatio[n] = ffunc->Evaluate();
            }

            if (m_session->DefinesFunction("SVVDiffCoeff", varName))
            {
                 ASSERTL0(m_useSpecVanVisc[n], "SVV cut off ratio is not set for the variable:" + varName);
                LibUtilities::EquationSharedPtr ffunc
                    = m_session->GetFunction("SVVDiffCoeff", varName);
                m_sVVDiffCoeff[n] = ffunc->Evaluate();
            }
        }

        // creation of the extrapolation object
        if(m_equationType == eUnsteadyNavierStokes)
        {
            std::string vExtrapolation = "Standard";

            if (m_session->DefinesSolverInfo("Extrapolation"))
            {
                vExtrapolation = m_session->GetSolverInfo("Extrapolation");
            }

            m_extrapolation = GetExtrapolateFactory().CreateInstance(
                vExtrapolation,
                m_session,
                m_fields,
                m_pressure,
                m_velocity,
                m_advObject);
        }

        // Integrate only the convective fields
        for (n = 0; n < m_nConvectiveFields; ++n)
        {
            m_intVariables.push_back(n);
        }
        
        m_saved_aii_Dt = Array<OneD, NekDouble>(m_nConvectiveFields,
                                                NekConstants::kNekUnsetDouble);

        // Load parameters for Spectral Vanishing Viscosity
/*
        m_session->MatchSolverInfo("SpectralVanishingViscosity","True",
                                   m_useSpecVanVisc,false);
        m_session->LoadParameter("SVVCutoffRatio",m_sVVCutoffRatio,0.75);
        m_session->LoadParameter("SVVDiffCoeff",  m_sVVDiffCoeff,  0.1);
        // Needs to be set outside of next if so that it is turned off by default


        m_session->MatchSolverInfo("SpectralVanishingViscosityHomo1D","True",
                                   m_useHomo1DSpecVanVisc,false);
*/
        
        m_session->MatchSolverInfo("SPECTRALHPDEALIASING","True",
                                   m_specHP_dealiasing,false);


        if(m_HomogeneousType == eHomogeneous1D)
        {
            ASSERTL0(m_nConvectiveFields > 2,"Expect to have three velocity fields with homogenous expansion");

/*
            if(m_useHomo1DSpecVanVisc == false)
            {
                m_session->MatchSolverInfo("SpectralVanishingViscosity","True",m_useHomo1DSpecVanVisc,false);
            }
*/

            for (int i = 0; i < m_nConvectiveFields; ++i) 
            {
               m_useHomo1DSpecVanVisc[i] = m_useSpecVanVisc[i];
               
               if(m_useHomo1DSpecVanVisc[i])
               {
                   Array<OneD, unsigned int> planes;
                   planes = m_fields[0]->GetZIDs();

                   int num_planes = planes.num_elements();
                   Array<OneD, NekDouble> SVV(num_planes,0.0);
                   NekDouble fac;
                   int kmodes = m_fields[0]->GetHomogeneousBasis()->GetNumModes();
                   int pstart;
   
                   pstart = m_sVVCutoffRatio[i]*kmodes;
                   
                   for(n = 0; n < num_planes; ++n)
                   {
                       if(planes[n] > pstart)
                       {
                           fac = (NekDouble)((planes[n] - kmodes)*(planes[n] - kmodes))/
                               ((NekDouble)((planes[n] - pstart)*(planes[n] - pstart)));
                           SVV[n] = m_sVVDiffCoeff[i]*exp(-fac)/m_diffCoeff[i];
                       }
                   }

/*
                   for(int j = 0; j < m_velocity.num_elements(); ++j)
                   {
                       m_fields[m_velocity[j]]->SetHomo1DSpecVanVisc(SVV);
                   }
*/
                   m_fields[i]->SetHomo1DSpecVanVisc(SVV);
               }
            }            
        }

        m_session->MatchSolverInfo("SmoothAdvection", "True", m_SmoothAdvection, false);

        // set explicit time-intregration class operators
        m_ode.DefineOdeRhs(&VelocityCorrectionScheme::EvaluateAdvection_SetPressureBCs, this);

        m_extrapolation->SubSteppingTimeIntegration(m_intScheme->GetIntegrationMethod(), m_intScheme);
        m_extrapolation->GenerateHOPBCMap();
        
        // set implicit time-intregration class operators
        m_ode.DefineImplicitSolve(&VelocityCorrectionScheme::SolveUnsteadyStokesSystem,this);
    }
    
    /**
     * Destructor
     */
    VelocityCorrectionScheme::~VelocityCorrectionScheme(void)
    {        
    }
    
    /**
     * 
     */
    void VelocityCorrectionScheme::v_GenerateSummary(SolverUtils::SummaryList& s)
    {
        UnsteadySystem::v_GenerateSummary(s);

        if (m_extrapolation->GetSubStepIntegrationMethod() !=
            LibUtilities::eNoTimeIntegrationMethod)
        {
            SolverUtils::AddSummaryItem(s, "Substepping", 
                             LibUtilities::TimeIntegrationMethodMap[
                              m_extrapolation->GetSubStepIntegrationMethod()]);
        }

        string dealias = m_homogen_dealiasing ? "Homogeneous1D" : "";
        if (m_specHP_dealiasing)
        {
            dealias += (dealias == "" ? "" : " + ") + string("spectral/hp");
        }
        if (dealias != "")
        {
            SolverUtils::AddSummaryItem(s, "Dealiasing", dealias);
        }

        for (int i = 0; i< m_nConvectiveFields; ++i)
        {
            string smoothing = m_useSpecVanVisc[i] ? "spectral/hp" : "";
            if (m_useHomo1DSpecVanVisc[i])
            {
                smoothing += (smoothing == "" ? "" : " + ") + string("Homogeneous1D");
            }
            if (smoothing != "")
            {
                SolverUtils::AddSummaryItem(
                    s, "Smoothing for var " + m_session->GetVariable(i), "SVV (" + smoothing + " SVV (cut-off = "
                    + boost::lexical_cast<string>(m_sVVCutoffRatio[i])
                    + ", diff coeff = "
                    + boost::lexical_cast<string>(m_sVVDiffCoeff[i])+")");
            }
        }
    }

    /**
     * 
     */
    void VelocityCorrectionScheme::v_DoInitialise(void)
    {

        UnsteadySystem::v_DoInitialise();

        // Set up Field Meta Data for output files
        m_fieldMetaDataMap["Kinvis"]   = boost::lexical_cast<std::string>(m_kinvis);
        m_fieldMetaDataMap["TimeStep"] = boost::lexical_cast<std::string>(m_timestep);

        // set boundary conditions here so that any normal component
        // correction are imposed before they are imposed on intiial
        // field below
        SetBoundaryConditions(m_time);

        for(int i = 0; i < m_nConvectiveFields; ++i)
        {
            m_fields[i]->LocalToGlobal();
            m_fields[i]->ImposeDirichletConditions(m_fields[i]->UpdateCoeffs());
            m_fields[i]->GlobalToLocal();
            m_fields[i]->BwdTrans(m_fields[i]->GetCoeffs(),
                                  m_fields[i]->UpdatePhys());
        }
    }
    

    /**
     * 
     */
    void VelocityCorrectionScheme:: v_TransCoeffToPhys(void)
    {
        int nfields = m_fields.num_elements() - 1;
        for (int k=0 ; k < nfields; ++k)
        {
            //Backward Transformation in physical space for time evolution
            m_fields[k]->BwdTrans_IterPerExp(m_fields[k]->GetCoeffs(),
                                             m_fields[k]->UpdatePhys());
        }
    }
    
    /**
     * 
     */
    void VelocityCorrectionScheme:: v_TransPhysToCoeff(void)
    {
        
        int nfields = m_fields.num_elements() - 1;
        for (int k=0 ; k < nfields; ++k)
        {
            //Forward Transformation in physical space for time evolution
            m_fields[k]->FwdTrans_IterPerExp(m_fields[k]->GetPhys(),m_fields[k]->UpdateCoeffs());
        }
    }
    
    /**
     * 
     */
    Array<OneD, bool> VelocityCorrectionScheme::v_GetSystemSingularChecks()
    {
        int vVar = m_session->GetVariables().size();
        Array<OneD, bool> vChecks(vVar, false);
        vChecks[vVar-1] = true;
        return vChecks;
    }
    
    /**
     * 
     */
    int VelocityCorrectionScheme::v_GetForceDimension()
    {
        return m_session->GetVariables().size() - 1;
    }

    /**
     * Explicit part of the method - Advection, Forcing + HOPBCs
     */
    void VelocityCorrectionScheme::v_EvaluateAdvection_SetPressureBCs(
        const Array<OneD, const Array<OneD, NekDouble> > &inarray, 
        Array<OneD, Array<OneD, NekDouble> > &outarray, 
        const NekDouble time)
    {
        EvaluateAdvectionTerms(inarray, outarray);

        // Smooth advection
        if(m_SmoothAdvection)
        {
            for(int i = 0; i < m_nConvectiveFields; ++i)
            {
                m_pressure->SmoothField(outarray[i]);
            }
        }

        // Add forcing terms
        std::vector<SolverUtils::ForcingSharedPtr>::const_iterator x;
        for (x = m_forcing.begin(); x != m_forcing.end(); ++x)
        {
            (*x)->Apply(m_fields, inarray, outarray, time);
        }

        // Calculate High-Order pressure boundary conditions
        m_extrapolation->EvaluatePressureBCs(inarray,outarray,m_kinvis);
    }
    
    /**
     * Implicit part of the method - Poisson + nConv*Helmholtz
     */
    void VelocityCorrectionScheme::SolveUnsteadyStokesSystem(
        const Array<OneD, const Array<OneD, NekDouble> > &inarray, 
        Array<OneD, Array<OneD, NekDouble> > &outarray, 
        const NekDouble time, 
        const NekDouble aii_Dt)
    {
        int i;
        int phystot = m_fields[0]->GetTotPoints();

        Array<OneD, Array< OneD, NekDouble> > F(m_nConvectiveFields);
        for(i = 0; i < m_nConvectiveFields; ++i)
        {
            F[i] = Array<OneD, NekDouble> (phystot, 0.0);
        }

        // Enforcing boundary conditions on all fields
        SetBoundaryConditions(time);
        
        // Substep the pressure boundary condition if using substepping
        m_extrapolation->SubStepSetPressureBCs(inarray,aii_Dt,m_kinvis);
    
        // Set up forcing term for pressure Poisson equation
        SetUpPressureForcing(inarray, F, aii_Dt);

        // Solve Pressure System
        SolvePressure (F[0]);

        // Set up forcing term for Helmholtz problems
        SetUpViscousForcing(inarray, F, aii_Dt);
        
        // Solve velocity system
        SolveViscous( F, outarray, aii_Dt);
    }
        
    /**
     * Forcing term for Poisson solver solver
     */ 
    void   VelocityCorrectionScheme::v_SetUpPressureForcing(
        const Array<OneD, const Array<OneD, NekDouble> > &fields, 
        Array<OneD, Array<OneD, NekDouble> > &Forcing, 
        const NekDouble aii_Dt)
    {                
        int i;
        int physTot = m_fields[0]->GetTotPoints();
        int nvel = m_velocity.num_elements();
        Array<OneD, NekDouble> wk(physTot, 0.0);
        
        Vmath::Zero(physTot,Forcing[0],1);
        
        for(i = 0; i < nvel; ++i)
        {
            m_fields[i]->PhysDeriv(MultiRegions::DirCartesianMap[i],fields[i], wk);
            Vmath::Vadd(physTot,wk,1,Forcing[0],1,Forcing[0],1);
        }

        Vmath::Smul(physTot,1.0/aii_Dt,Forcing[0],1,Forcing[0],1);        
    }
    
    /**
     * Forcing term for Helmholtz solver
     */
    void   VelocityCorrectionScheme::v_SetUpViscousForcing(
        const Array<OneD, const Array<OneD, NekDouble> > &inarray, 
        Array<OneD, Array<OneD, NekDouble> > &Forcing, 
        const NekDouble aii_Dt)
    {
        NekDouble aii_dtinv = 1.0/aii_Dt;
        int phystot = m_fields[0]->GetTotPoints();

        // Grad p
        m_pressure->BwdTrans(m_pressure->GetCoeffs(),m_pressure->UpdatePhys());
        
        int nvel = m_velocity.num_elements();
        if(nvel == 2)
        {
            m_pressure->PhysDeriv(m_pressure->GetPhys(), Forcing[0], Forcing[1]);
        }
        else
        {
            m_pressure->PhysDeriv(m_pressure->GetPhys(), Forcing[0], 
                                  Forcing[1], Forcing[2]);
        }
        
        // Subtract inarray/(aii_dt) and divide by kinvis. Kinvis will
        // need to be updated for the convected fields.
        for(int i = 0; i < m_nConvectiveFields; ++i)
        {
            Blas::Daxpy(phystot,-aii_dtinv,inarray[i],1,Forcing[i],1);
            Blas::Dscal(phystot,1.0/m_diffCoeff[i],&(Forcing[i])[0],1);
        }
    }

    
    /**
     * Solve pressure system
     */
    void   VelocityCorrectionScheme::v_SolvePressure(
        const Array<OneD, NekDouble>  &Forcing)
    {
        StdRegions::ConstFactorMap factors;
        // Setup coefficient for equation
        factors[StdRegions::eFactorLambda] = 0.0;

        // Solver Pressure Poisson Equation
        m_pressure->HelmSolve(Forcing, m_pressure->UpdateCoeffs(), NullFlagList,
                              factors);
    }
    
    /**
     * Solve velocity system
     */
    void   VelocityCorrectionScheme::v_SolveViscous(
        const Array<OneD, const Array<OneD, NekDouble> > &Forcing,
        Array<OneD, Array<OneD, NekDouble> > &outarray,
        const NekDouble aii_Dt)
    {
        StdRegions::ConstFactorMap factors;
/*
        if(m_useSpecVanVisc)
        {
            factors[StdRegions::eFactorSVVCutoffRatio] = m_sVVCutoffRatio;
            factors[StdRegions::eFactorSVVDiffCoeff]   = m_sVVDiffCoeff/m_kinvis;
        }
*/

        // Solve Helmholtz system and put in Physical space
        for(int i = 0; i < m_nConvectiveFields; ++i)
        {
            // Setup SVV stuff
            if(m_useSpecVanVisc[i])
            {
               factors[StdRegions::eFactorSVVCutoffRatio] = m_sVVCutoffRatio[i];
               factors[StdRegions::eFactorSVVDiffCoeff]   = m_sVVDiffCoeff[i]/m_diffCoeff[i];
            }

            // Setup coefficients for equation
            factors[StdRegions::eFactorLambda] = 1.0/aii_Dt/m_diffCoeff[i];
            m_fields[i]->HelmSolve(Forcing[i], m_fields[i]->UpdateCoeffs(),
                                   NullFlagList, factors);
            m_fields[i]->BwdTrans(m_fields[i]->GetCoeffs(),outarray[i]);
        }
    }
    
} //end of namespace
