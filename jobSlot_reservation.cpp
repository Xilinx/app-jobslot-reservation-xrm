/*
* Copyright (C) 2021, Xilinx Inc - All rights reserved
* Xilinx U30 jobslot-reservation-xrm (jobslot-reservation-xrm)
*
* Licensed under the Apache License, Version 2.0 (the "License"). You may
* not use this file except in compliance with the License. A copy of the
* License is located at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
* WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
* License for the specific language governing permissions and limitations
* under the License.
*/

#include <string>
#include <iostream>
#include <cstdlib>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <syslog.h>
#include <xma.h>
#include <xrm.h>
#define MAX_CH_SIZE 16284
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
//#define DEBUG

using namespace std;
namespace pt=boost::property_tree;


struct kernel_load
{
   int dec_load;
   int scal_load;
   int enc_load;
   int enc_num;
   int la_load;
};


void fill_xrm_props(kernel_load* kernelLoad, xrmCuPoolProperty* xrm_transcode_cu_pool_prop);
void calc_xrm_load(char* describe_job, xrmContext* xrm_ctx, xrmCuPoolProperty* xrm_transcode_cu_pool_prop);


int main (int argc, char *argv[])
{

    FILE *fp = NULL;
    struct stat dstat;
    int ret = -1;

    if (stat("/var/tmp/xilinx", &dstat) == -1)
    {
       ret = mkdir("/var/tmp/xilinx",0777);
       if (ret != 0)
       {
          printf("Couldnt create /var/tmp/xilinx folder");
          return EXIT_FAILURE;
       }         
    }

    fp = fopen ("/var/tmp/xilinx/xrm_jobReservation.sh", "w");

    if (fp==NULL)
    {
      printf("Couldnt create xrm_jobReservation.sh at /var/tmp/xilinx/\n");
      return EXIT_FAILURE;
    }

    fputs("source /opt/xilinx/xrt/setup.sh\n", fp);
    fputs("source /opt/xilinx/xrm/setup.sh\n", fp);

    fputs("export LD_LIBRARY_PATH=/opt/xilinx/ffmpeg/lib:$LD_LIBRARY_PATH\n", fp);
    fputs("export PATH=/opt/xilinx/ffmpeg/bin:/opt/xilinx/xcdr/bin:$PATH\n",fp);

    char ch_xrm_id[2048];

    if (argc != 2)
    {
        printf ("Usage:\n");
        printf ("    %s <job description file name>\n", argv[0]);
        return -1;
    }

    char describe_job[MAX_CH_SIZE];
    int  num_pool_avl =0;
    uint64_t transcode_reservation_id[512];


    xrmCuPoolProperty xrm_transcode_cu_pool_prop;
    memset(&xrm_transcode_cu_pool_prop, 0, sizeof(xrm_transcode_cu_pool_prop));


    xrmContext *xrm_ctx = (xrmContext *)xrmCreateContext(XRM_API_VERSION_1);
    if (xrm_ctx == NULL)
    {
       printf("Test: create context failed\n");
       return -1;
    }

    strcpy(describe_job,argv[1]);
    //check if describe job is existing  
    if (access(describe_job,F_OK)==-1)
    {
       printf("ERROR: describe job (%s) is not found.\n\n",describe_job);
       return -1;
    }

    calc_xrm_load(describe_job, xrm_ctx, &xrm_transcode_cu_pool_prop);

    num_pool_avl =  xrmCheckCuPoolAvailableNum(xrm_ctx, &xrm_transcode_cu_pool_prop);
    if (num_pool_avl < 0)
    {
       printf("ERROR: Fail to reserve job slot for given %s.\n", describe_job);
       return -1;
    }
    else
    {
       printf("\n\nFor %s, Possible number of job slots available = %d\n\n", describe_job, num_pool_avl);
    }

    for (int ncl=0, idx=1; ncl<num_pool_avl; ncl++,idx++)
    {
       transcode_reservation_id[idx] = xrmCuPoolReserve(xrm_ctx, &xrm_transcode_cu_pool_prop);
       //printf("------------xrm_reservationid [%d]: %lu\n", idx,transcode_reservation_id[idx]);
       sprintf(ch_xrm_id,"export XRM_RESERVE_ID_%d=%lu\n",idx,transcode_reservation_id[idx]);
       fputs(ch_xrm_id, fp);

#ifdef DEBUG  
	if (transcode_reservation_id[idx] == 0)
		printf("xrm_cu_pool_reserve: fail to reserve transcode cu pool\n");
	else
        {
                // query the reserve result
                xrmCuPoolResource transcode_cu_pool_res;
                memset(&transcode_cu_pool_res, 0, sizeof(transcode_cu_pool_res));

                int ret = xrmReservationQuery(xrm_ctx, transcode_reservation_id[idx], &transcode_cu_pool_res);
                if (ret != 0)
                   printf("xrm_reservation_query: fail to query reserved  cu list\n");
                else
                {
                   for (int i = 0; i < (xrm_transcode_cu_pool_prop.cuListProp.cuNum); i++)
                   {
                       printf("query the reserved cu list: cu %d\n", i);
                       printf("   xclbin_file_name is:  %s\n", transcode_cu_pool_res.cuResources[i].xclbinFileName);
                       printf("   kernelPluginFileName is:  %s\n", transcode_cu_pool_res.cuResources[i].kernelPluginFileName);
                       printf("   kernel_name is:  %s\n", transcode_cu_pool_res.cuResources[i].kernelName);
                       printf("   kernel_alias is:  %s\n", transcode_cu_pool_res.cuResources[i].kernelAlias);
                       printf("   device_id is:  %d\n", transcode_cu_pool_res.cuResources[i].deviceId);
                       printf("   cu_id is:  %d\n", transcode_cu_pool_res.cuResources[i].cuId);
                       printf("   cu_type is:  %d\n", transcode_cu_pool_res.cuResources[i].cuType);
		  }
                }      
        }
#endif

    }
    fclose (fp);

    printf("\n------------------------------------------------------------------------------\nThe Job_slot_reservations are alive as long as this Application is alive!\n(press Enter to end)\n ------------------------------------------------------------------------------\n");
    while(1)
    {
       char endKey = getchar();
       if((endKey == 13)||(endKey == '\n'))
       {
         for (int i=1; ((i<=num_pool_avl) & (num_pool_avl>0)) ;i++)
         {
            xrmCuPoolRelinquish (xrm_ctx, transcode_reservation_id[i]);
            //printf("------------xrmCuPoolRelinquish [%d] =%lu\n",i,transcode_reservation_id[i]);
         }
         return 0;
       }
    }


}



void fill_xrm_props(kernel_load* kernelLoad, xrmCuPoolProperty* xrm_transcode_cu_pool_prop)
{
    xrm_transcode_cu_pool_prop->cuListNum = 1;
    xrm_transcode_cu_pool_prop->cuListProp.sameDevice = true;
    int index=0;

    if (kernelLoad->dec_load> 0)
    {
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "decoder");
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "DECODER_MPSOC");
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(kernelLoad->dec_load);
        index++;
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "kernel_vcu_decoder");
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "");
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
        index++;
    } 
    if (kernelLoad->scal_load > 0)
    {
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "scaler");
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "SCALER_MPSOC");
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(kernelLoad->scal_load);
        index++;
    }
    if (kernelLoad->enc_load > 0)
    {
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "encoder");
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "ENCODER_MPSOC");
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(kernelLoad->enc_load);
        index++;

        for (int skrnl=0; skrnl< kernelLoad->enc_num; skrnl++)
        {
            strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "kernel_vcu_encoder");
            strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "");
            xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
            xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
            index++;
        }
    }
    if (kernelLoad->la_load > 0)
    {
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "lookahead");
        strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "LOOKAHEAD_MPSOC");
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
        xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(kernelLoad->la_load);
        index++;
    }
    xrm_transcode_cu_pool_prop->cuListProp.cuNum = index;
}



void calc_xrm_load(char* describe_job, xrmContext* xrm_ctx, xrmCuPoolProperty* xrm_transcode_cu_pool_prop)
{
    char pluginName[XRM_MAX_NAME_LEN];
    int func_id = 0;
    kernel_load kernelLoad;
    xrmPluginFuncParam param;
    memset(&param, 0, sizeof(xrmPluginFuncParam));

    //read the job description 
    pt::ptree job;    
    pt::read_json(describe_job,job);

    std::stringstream jobStr;
    boost::property_tree::write_json(jobStr, job);

    strncpy(param.input,jobStr.str().c_str(),MAX_CH_SIZE-1);

    //calculate required kenrel loads
    //decoder
    strcpy(pluginName, "xrmU30DecPlugin");
    if (xrmExecPluginFunc(xrm_ctx, pluginName, func_id, &param) != XRM_SUCCESS)
        printf("decoder plugin function=%d fail to run the function\n",func_id);
    else
    {
        kernelLoad.dec_load = atoi((char*)(strtok(param.output, " ")));
#ifdef DEBUG   
        printf("decder_load:%d\n",kernelLoad.dec_load);
#endif
    }

    //scaler
    strcpy(pluginName, "xrmU30ScalPlugin");
    if (xrmExecPluginFunc(xrm_ctx, pluginName, func_id, &param) != XRM_SUCCESS)
       printf("scaler plugin function=%d fail to run the function\n",func_id);					
    else
    {
       kernelLoad.scal_load = atoi((char*)(strtok(param.output, " ")));
#ifdef DEBUG  
       printf("scaler_load:%d\n",kernelLoad.scal_load);
#endif
    }

    //encoder+lookahead
    strcpy(pluginName, "xrmU30EncPlugin");
    if (xrmExecPluginFunc(xrm_ctx, pluginName, func_id, &param) != XRM_SUCCESS)
	printf("encoder plugin function=%d fail to run the function\n",func_id);
    else
    {
        kernelLoad.enc_load = atoi((char*)(strtok(param.output, " ")));
        kernelLoad.enc_num = atoi((char*)(strtok(NULL, " ")));

        kernelLoad.la_load = atoi((char*)(strtok(NULL, " ")));
#ifdef DEBUG  
	printf("encoder_load:%d number_of_encoders=%d la_load=%d\n ",kernelLoad.enc_load,kernelLoad.enc_num,kernelLoad.la_load);
#endif
    } 

    fill_xrm_props (&kernelLoad, xrm_transcode_cu_pool_prop); 
}
