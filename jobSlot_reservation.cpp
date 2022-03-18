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
#define MAX_DEVS_PER_CMD 2
//#define DEBUG

using namespace std;
namespace pt=boost::property_tree;


struct kernel_load
{
   int dec_load[MAX_DEVS_PER_CMD];
   int scal_load[MAX_DEVS_PER_CMD];
   int enc_load[MAX_DEVS_PER_CMD];
   int enc_num[MAX_DEVS_PER_CMD];
   int la_load[MAX_DEVS_PER_CMD];
};

static int start_cu_idx[MAX_DEVS_PER_CMD];
static int num_dev_per_cmd;

void fill_xrm_props(kernel_load* kernelLoad, xrmCuPoolPropertyV2* xrm_transcode_cu_pool_prop);
void calc_xrm_load(char* describe_job, xrmContext* xrm_ctx, xrmCuPoolPropertyV2* xrm_transcode_cu_pool_prop);


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
    fputs("export PATH=/opt/xilinx/ffmpeg/bin:/opt/xilinx/xcdr/bin:$PATH\n\n",fp);

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


    xrmCuPoolPropertyV2* xrm_transcode_cu_pool_prop;  
    xrmCuListResInforV2* cuListResInfor;
    xrmCuPoolResInforV2* xrm_transcode_cu_pool_res;
    xrmCuResInforV2* cuResInfor;

    xrm_transcode_cu_pool_prop = (xrmCuPoolPropertyV2*)calloc(1, sizeof(xrmCuPoolPropertyV2));
    if (xrm_transcode_cu_pool_prop == NULL) {
       printf("ERROR: Unable to allocate memory via calloc()\n");
       return -1;
    }

    xrm_transcode_cu_pool_res = (xrmCuPoolResInforV2*)calloc(1, sizeof(xrmCuPoolResInforV2));
    if (xrm_transcode_cu_pool_res == NULL) {
       printf("ERROR: Unable to allocate memory via calloc()\n");
       return -1;
    }

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
    calc_xrm_load(describe_job, xrm_ctx, xrm_transcode_cu_pool_prop);

    num_pool_avl =  xrmCheckCuPoolAvailableNumV2(xrm_ctx, xrm_transcode_cu_pool_prop);
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
       transcode_reservation_id[idx] = xrmCuPoolReserveV2(xrm_ctx, xrm_transcode_cu_pool_prop, xrm_transcode_cu_pool_res);
       //printf("------------xrm_reservationid [%d]: %lu\n", idx,transcode_reservation_id[idx]);
       sprintf(ch_xrm_id,"export XRM_RESERVE_ID_%d=%lu\n",idx,transcode_reservation_id[idx]);
       fputs(ch_xrm_id, fp);

       if (num_dev_per_cmd > 1)
       {   
          for (int nd=0; nd<num_dev_per_cmd; nd++)
          {
             cuListResInfor = &(xrm_transcode_cu_pool_res->cuListResInfor[0]);
             cuResInfor = &( cuListResInfor->cuResInfor[start_cu_idx[nd]]);
             sprintf(ch_xrm_id,"var_dev_%d_%d=%d\n",idx,nd,cuResInfor->deviceId);
             fputs(ch_xrm_id, fp);
          }
          sprintf(ch_xrm_id,"\n");
          fputs(ch_xrm_id, fp);
       }              
    }

    free (xrm_transcode_cu_pool_res);
    free (xrm_transcode_cu_pool_prop);      
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

void fill_xrm_props(kernel_load* kernelLoad, xrmCuPoolPropertyV2* xrm_transcode_cu_pool_prop)
{
   int index=0;
   uint64_t deviceInfoContraintType = XRM_DEVICE_INFO_CONSTRAINT_TYPE_VIRTUAL_DEVICE_INDEX;
   uint64_t deviceInfoDeviceIndex = 0;    

   xrm_transcode_cu_pool_prop->cuListNum = 1;
   num_dev_per_cmd = 0;

   for (int nd=0; nd<MAX_DEVS_PER_CMD; nd++)
   {      
      start_cu_idx[nd] = index;
      if (kernelLoad->dec_load[nd]> 0)
      {
         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "decoder");
         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "DECODER_MPSOC");
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);        
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(kernelLoad->dec_load[nd]);
         index++;

         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "kernel_vcu_decoder");
         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "");
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
         index++;
      }

      if (kernelLoad->scal_load[nd] > 0)
      {
         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "scaler");
         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "SCALER_MPSOC");
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(kernelLoad->scal_load[nd]);
         index++;
      }

      if (kernelLoad->enc_load[nd] > 0)
      {
         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "encoder");
         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "ENCODER_MPSOC");
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(kernelLoad->enc_load[nd]);
         index++;

         for (int skrnl=0; skrnl< kernelLoad->enc_num[nd]; skrnl++)
         {
            strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "kernel_vcu_encoder");
            strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "");
            xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
            xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
            xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
            index++;
         }
      }

      if (kernelLoad->la_load[nd] > 0)
      {
         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelName, "lookahead");
         strcpy(xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].kernelAlias, "LOOKAHEAD_MPSOC");
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].devExcl = false;
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
         xrm_transcode_cu_pool_prop->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(kernelLoad->la_load[nd]);
         index++;
      }

      deviceInfoDeviceIndex = ++deviceInfoDeviceIndex;
      if (start_cu_idx[nd] < index)
         num_dev_per_cmd = num_dev_per_cmd+1;
   }
   xrm_transcode_cu_pool_prop->cuListProp.cuNum = index;
}

void calc_xrm_load(char* describe_job, xrmContext* xrm_ctx, xrmCuPoolPropertyV2* xrm_transcode_cu_pool_prop)
{
   char pluginName[XRM_MAX_NAME_LEN];
   int func_id = 0, skip = 0;
   char* endptr;
   char* token;
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
      for (int nd=0; nd< MAX_DEVS_PER_CMD; nd++)
      {
         errno = 0;
         if (nd==0) 
            token = (char*)(strtok(param.output, " ")); 
         else
            token = (char*)(strtok(NULL, " "));
             
         if (token ==NULL)
            break;  

         kernelLoad.dec_load[nd] = (int) strtol(token, &endptr, 0); 

         if (kernelLoad.dec_load[nd] > XRM_MAX_CU_LOAD_GRANULARITY_1000000)
         {
            fprintf (stderr, "requested decoder load =%d exceeds maximum capcity.\n",kernelLoad.dec_load[nd]);
            return -1;
         }
         else 
            fprintf (stderr, "[%d]: decoder plugin function =%d success to run the function, output_load:%d\n",nd,func_id,kernelLoad.dec_load[nd]);

         skip = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0); //number of instances

         //check for strtol errors
         if (errno != 0)
         {
            perror("strtol");
            return -1;
         }
#ifdef DEBUG   
        printf("decder_load:%d\n",kernelLoad.dec_load[nd]);
#endif           
      }                                        
   }   

   //scaler
   strcpy(pluginName, "xrmU30ScalPlugin");
   if (xrmExecPluginFunc(xrm_ctx, pluginName, func_id, &param) != XRM_SUCCESS)
      printf("scaler plugin function=%d fail to run the function\n",func_id);					
   else
   {
      for (int nd=0; nd < MAX_DEVS_PER_CMD; nd++)
      {
         errno = 0;
         if (nd==0) 
            token = (char*)(strtok(param.output, " ")); 
         else
            token = (char*)(strtok(NULL, " "));
             
         if (token ==NULL)
            break; 

         kernelLoad.scal_load[nd] = (int) strtol(token, &endptr, 0);

         if (kernelLoad.scal_load[nd] > XRM_MAX_CU_LOAD_GRANULARITY_1000000)
         {
            fprintf (stderr, "requested scaler load =%d exceeds maximum capcity.\n",kernelLoad.scal_load[nd]);
            return -1;
         }
         else
            fprintf (stderr, "[%d]: scaler plugin function =%d success to run the function, output_load:%d\n", nd,func_id,kernelLoad.scal_load[nd]);
 
         skip = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0); //number of instances

         //check for strtol errors
         if (errno != 0)
         {
            perror("strtol");
            return -1;
         }
#ifdef DEBUG  
         printf("scaler_load:%d\n",kernelLoad.scal_load[nd]);
#endif
      }       
   }

   //encoder+lookahead
   strcpy(pluginName, "xrmU30EncPlugin");
   if (xrmExecPluginFunc(xrm_ctx, pluginName, func_id, &param) != XRM_SUCCESS)
	   printf("encoder plugin function=%d fail to run the function\n",func_id);
   else
   {
      for (int nd=0; nd< MAX_DEVS_PER_CMD; nd++)
      {
         errno = 0; 
         if (nd==0) 
            token = (char*)(strtok(param.output, " ")); 
         else
            token = (char*)(strtok(NULL, " "));
             
         if (token ==NULL)
            break;

         kernelLoad.enc_load[nd] = (int) strtol(token, &endptr, 0);

         kernelLoad.enc_num[nd] = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0);
 
         kernelLoad.la_load[nd] = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0);

         //check for strtol errors
         if (errno != 0)
         {
            perror("strtol");
            return -1;
         }

         if (kernelLoad.enc_load[nd] > XRM_MAX_CU_LOAD_GRANULARITY_1000000)
         {
            fprintf (stderr, "requested encoder load =%d exceeds maximum capcity.\n",kernelLoad.enc_load[nd]);
            return -1;
         }
         else
            fprintf (stderr, "[%d]: encoder plugin function =%d success to run the function, output_load:%d enc_num=%d la_load=%d\n",nd,func_id,kernelLoad.enc_load[nd],kernelLoad.enc_num[nd],kernelLoad.la_load[nd]);
#ifdef DEBUG  
	      printf("encoder_load:%d number_of_encoders=%d la_load=%d\n ",kernelLoad.enc_load[nd],kernelLoad.enc_num[nd],kernelLoad.la_load[nd]);
#endif               
      }
   }

   fill_xrm_props (&kernelLoad, xrm_transcode_cu_pool_prop); 
}
