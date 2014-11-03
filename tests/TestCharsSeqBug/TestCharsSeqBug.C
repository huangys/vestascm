// Copyright (C) 2001, Compaq Computer Corporation
// 
// This file is part of Vesta.
// 
// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include <Basics.H>
#include <chars_seq.H>

using std::endl;
using std::cerr;
using std::cout;

char *list1[] = {
  "PATH=/h/tJJ/McDdIbQ/bA_EeDylDwR/ola/UjN/v69l5IB60x/ITtc_QzMNM/EYzgZKlg/J65Am30j:/SoL:/lAO/QPg:/zAngR422585765Yad",
  "PERL5LIB=/G/VgE/MDJ/KWfdh/YyW/COKB/:/J/BqH/hXUiO/zMB/blJG:/OMi/vfqTA/53v3.3/hDQ/IilB0/iAeB_orDg",
  "MODEL_ROOT=/b/FjL/uUG/nlKMmc/PRCQCP/WyIFKA-ZNGa-gBs-uN0-51CW35v",
  "LM_LICENSE_FILE=/ZsU/Gjig/MBq/MUhWdVs/gtyg/bcX/yGOhFftM/RpI_rWL01:/DoK/MzBi/qKl/TNlXppe/FYNr/ZcV/ASwmjYe/UOH_JEn97:/LNt/LwwX/yFr/gKRJMiy/ohrt/ihI/FSvsOsfg/hef_Fqi04:/bRk/vibY/owb/TczkmNs/trYM/oeD/AiasBaxoDfPaa/mqC_Gig61:/VNF/IIvo/tpU/vkAHsCD/plnw/wyk/bOoSYBi/JiZ_UZp18:/bks/OIlk/eng/uicEsCR/Qskf/pCK/hhfxd/DEl_AQL88:/OAR/vFVC/vYL/VtDUtgC/YTqE/xgz/ohEhpA/KcN_euF97:/BUQ/CYPH/YTI/nXRGZAN/jTap/Snf/JcZLTvN/qZJ_vxA02:/AfV/dBuk/wOy/cwoJUnI/ztE/fFq/VEIohAjo/RwY_uSJ68:/QxQ/xrvn/xrb/rikCQcB/UA/ApR/IxvkizXE/mYe_Qam91:/vSk/gSGY/AqJ/NDahKjB/Wh/rhL/Xnfvsyau/VHZ_qbZ13:/Yuk/FTmS/Qbz/uPjAtvK/Fn/YKW/tCKFuadx/FXe-rEZ_pPN34:/Opw/IYQJ/hEB/mRHWtct/iy/xfl/fpTSVkUF/jWl-WeS_iLH55:/Fus/GEmq/pPK/Ljkjtyz/faB/eWV/kKMUpfPx/rXa_qDN99:0498@PZAAg480.LeG.paBND.lrh:0537@bOnv4173.UAk.DpNZH.wuJ:0160@MyESbOl226.mCU.PiEKA.kcY:1745@YxiD2404.AZ.SfJxm.Uba:8631@nGHE1603.iq.AoExd.lBC:0168@TAuV2367.SH.UxqoT.TVB",
  "CHEF_HOME=/m/teU/rgpeavd/Uj_JUnHEenj/AIg/FpZ/Vuhgr/hniE_CvaFa/QgCL/r64JN15x",
  "CAD_ROOT=/ZpJ/zZhF/FCD/DvDdH/hvywEGo/wYB",
  "SYSTEMSIM_ROOT=/Hwc/heDB/YBl/grGrp/fRJlmrI/nef/nPLwmTHG/PexsNXue/X9.6.8F"
};

char *list2[] = {
  "/usr/bin/ar",
  "-q",
  "-c",
  "libCommTalk",
  "CommunicationTower.o",
  "rotate_left__34AVLSet__T9GnuStringP12CommunicatorXPP38AVLSetNode__T9GnuStringP12C2e3q28f.o",
  "rotate_right__34AVLSet__T9GnuStringP12CommunicatorXPP38AVLSetNode__T9GnuStringP1235p46sc.o"
};

void Test(chars_seq &result, char *data[], unsigned int data_len)
{
  for(unsigned int i = 0; i < data_len; i++)
    {
      result.append(data[i]);
    }

  for(unsigned int i = 0; i < data_len; i++)
    {
      if(strcmp(result[i], data[i]) != 0)
	{
	  cerr << "Misconpare at " << i << ":" << endl
	       << "  orig = " << data[i] << endl
	       << "  copy = " << result[i] << endl;
	  exit(1);
	}
    }
  
}

int main()
{
  chars_seq *result = NEW_CONSTR(chars_seq, (10, 20));

  Test(*result, list1, sizeof(list1)/sizeof(list1[0]));

  delete result;

  result = NEW_CONSTR(chars_seq, (5, 100));

  Test(*result, list2, sizeof(list2)/sizeof(list2[0]));

  delete result;

  cout << "All tests succeeded!" << endl;
  return 0;
}
