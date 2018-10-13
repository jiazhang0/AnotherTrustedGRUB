#include <grub/err.h>
#include <grub/i18n.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/tpm.h>
#include <grub/mm.h>
#include <grub/tpm.h>
#include <grub/term.h>

static grub_efi_guid_t tpm_guid = EFI_TPM_GUID;
static grub_efi_guid_t tpm2_guid = EFI_TPM2_GUID;

static grub_efi_boolean_t grub_tpm_present(grub_efi_tpm_protocol_t *tpm)
{
  grub_efi_status_t status;
  TCG_EFI_BOOT_SERVICE_CAPABILITY caps;
  grub_uint32_t flags;
  grub_efi_physical_address_t eventlog, lastevent;

  caps.Size = (grub_uint8_t)sizeof(caps);

  status = efi_call_5(tpm->status_check, tpm, &caps, &flags, &eventlog,
		      &lastevent);

  if (status != GRUB_EFI_SUCCESS || caps.TPMDeactivatedFlag
      || !caps.TPMPresentFlag)
    return 0;

  return 1;
}

static grub_efi_boolean_t grub_tpm2_present(grub_efi_tpm2_protocol_t *tpm)
{
  grub_efi_status_t status;
  EFI_TCG2_BOOT_SERVICE_CAPABILITY caps;

  caps.Size = (grub_uint8_t)sizeof(caps);

  status = efi_call_2(tpm->get_capability, tpm, &caps);

  if (status != GRUB_EFI_SUCCESS || !caps.TPMPresentFlag)
    return 0;

  return 1;
}

static grub_efi_boolean_t grub_tpm_handle_find(grub_efi_handle_t *tpm_handle,
					       grub_efi_uint8_t *protocol_version)
{
  grub_efi_handle_t *handles;
  grub_efi_uintn_t num_handles;

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &tpm_guid, NULL,
				    &num_handles);
  if (handles && num_handles > 0) {
    *tpm_handle = handles[0];
    *protocol_version = 1;
    return 1;
  }

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &tpm2_guid, NULL,
				    &num_handles);
  if (handles && num_handles > 0) {
    *tpm_handle = handles[0];
    *protocol_version = 2;
    return 1;
  }

  return 0;
}

static grub_err_t
grub_tpm1_execute(grub_efi_handle_t tpm_handle,
		  PassThroughToTPM_InputParamBlock *inbuf,
		  PassThroughToTPM_OutputParamBlock *outbuf)
{
  grub_efi_status_t status;
  grub_efi_tpm_protocol_t *tpm;
  grub_uint32_t inhdrsize = sizeof(*inbuf) - sizeof(inbuf->TPMOperandIn);
  grub_uint32_t outhdrsize = sizeof(*outbuf) - sizeof(outbuf->TPMOperandOut);

  tpm = grub_efi_open_protocol (tpm_handle, &tpm_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (!grub_tpm_present(tpm))
    return 0;

  /* UEFI TPM protocol takes the raw operand block, no param block header */
  status = efi_call_5 (tpm->pass_through_to_tpm, tpm,
		       inbuf->IPBLength - inhdrsize, inbuf->TPMOperandIn,
		       outbuf->OPBLength - outhdrsize, outbuf->TPMOperandOut);

  switch (status) {
  case GRUB_EFI_SUCCESS:
    return 0;
  case GRUB_EFI_DEVICE_ERROR:
    return grub_error (GRUB_ERR_IO, N_("Command failed"));
  case GRUB_EFI_INVALID_PARAMETER:
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Invalid parameter"));
  case GRUB_EFI_BUFFER_TOO_SMALL:
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Output buffer too small"));
  case GRUB_EFI_NOT_FOUND:
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("TPM unavailable"));
  default:
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("Unknown TPM error"));
  }
}

static grub_err_t
grub_tpm2_execute(grub_efi_handle_t tpm_handle,
		  PassThroughToTPM_InputParamBlock *inbuf,
		  PassThroughToTPM_OutputParamBlock *outbuf)
{
  grub_efi_status_t status;
  grub_efi_tpm2_protocol_t *tpm;
  grub_uint32_t inhdrsize = sizeof(*inbuf) - sizeof(inbuf->TPMOperandIn);
  grub_uint32_t outhdrsize = sizeof(*outbuf) - sizeof(outbuf->TPMOperandOut);

  tpm = grub_efi_open_protocol (tpm_handle, &tpm2_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (!grub_tpm2_present(tpm))
    return 0;

  /* UEFI TPM protocol takes the raw operand block, no param block header */
  status = efi_call_5 (tpm->submit_command, tpm,
		       inbuf->IPBLength - inhdrsize, inbuf->TPMOperandIn,
		       outbuf->OPBLength - outhdrsize, outbuf->TPMOperandOut);

  switch (status) {
  case GRUB_EFI_SUCCESS:
    return 0;
  case GRUB_EFI_DEVICE_ERROR:
    return grub_error (GRUB_ERR_IO, N_("Command failed"));
  case GRUB_EFI_INVALID_PARAMETER:
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Invalid parameter"));
  case GRUB_EFI_BUFFER_TOO_SMALL:
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Output buffer too small"));
  case GRUB_EFI_NOT_FOUND:
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("TPM unavailable"));
  default:
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("Unknown TPM error"));
  }
}

grub_err_t
grub_tpm_execute(PassThroughToTPM_InputParamBlock *inbuf,
		 PassThroughToTPM_OutputParamBlock *outbuf)
{
  grub_efi_handle_t tpm_handle;
   grub_uint8_t protocol_version;

  /* It's not a hard failure for there to be no TPM */
  if (!grub_tpm_handle_find(&tpm_handle, &protocol_version))
    return 0;

  if (protocol_version == 1) {
    return grub_tpm1_execute(tpm_handle, inbuf, outbuf);
  } else {
    return grub_tpm2_execute(tpm_handle, inbuf, outbuf);
  }
}

static grub_err_t
grub_tpm1_log_event(grub_efi_handle_t tpm_handle, unsigned char *buf,
		    grub_size_t size, grub_uint8_t pcr,
		    const char *description)
{
  TCG_PCR_EVENT *event;
  grub_efi_status_t status;
  grub_efi_tpm_protocol_t *tpm;
  grub_efi_physical_address_t lastevent;
  grub_uint32_t algorithm;
  grub_uint32_t eventnum = 0;

  tpm = grub_efi_open_protocol (tpm_handle, &tpm_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (!grub_tpm_present(tpm))
    return 0;

  event = grub_zalloc(sizeof (TCG_PCR_EVENT) + grub_strlen(description) + 1);
  if (!event)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY,
		       N_("cannot allocate TPM event buffer"));

  event->PCRIndex  = pcr;
  event->EventType = EV_IPL;
  event->EventSize = grub_strlen(description) + 1;
  grub_memcpy(event->Event, description, event->EventSize);

  algorithm = TCG_ALG_SHA;
  status = efi_call_7 (tpm->log_extend_event, tpm, (grub_efi_physical_address_t)buf, (grub_uint64_t) size,
		       algorithm, event, &eventnum, &lastevent);
  grub_free (event);

  switch (status) {
  case GRUB_EFI_SUCCESS:
    return 0;
  case GRUB_EFI_DEVICE_ERROR:
    return grub_error (GRUB_ERR_IO, N_("Command failed"));
  case GRUB_EFI_INVALID_PARAMETER:
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Invalid parameter"));
  case GRUB_EFI_BUFFER_TOO_SMALL:
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Output buffer too small"));
  case GRUB_EFI_NOT_FOUND:
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("TPM unavailable"));
  default:
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("Unknown TPM error"));
  }
}

static grub_err_t
grub_tpm2_log_event(grub_efi_handle_t tpm_handle, unsigned char *buf,
		   grub_size_t size, grub_uint8_t pcr,
		   const char *description)
{
  EFI_TCG2_EVENT *event;
  grub_efi_status_t status;
  grub_efi_tpm2_protocol_t *tpm;
  grub_uint64_t flags;
  grub_size_t event_size;
  int is_pecoff;
  EFI_IMAGE_LOAD_EVENT *image_load;

  tpm = grub_efi_open_protocol (tpm_handle, &tpm2_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (!grub_tpm2_present(tpm))
    return 0;

  if (size > 2 && !grub_memcmp (buf, "MZ", 2)) {
    is_pecoff = 1;
    flags = PE_COFF_IMAGE;
    // FilePathSize  = (UINT32) GetDevicePathSize (FilePath);
    event_size = sizeof (*image_load) - sizeof (image_load->DevicePath) + //FilePathSize;
  } else {
    is_pecoff = 0;
    flags = 0;
    event_size = grub_strlen(description);
    grub_memcpy(event->Event, description, event_size);
  }

  event = grub_zalloc(sizeof (*event) - sizeof(event->Event) + event_size);
  if (!event)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY,
               N_("cannot allocate TPM event buffer"));

  event->Header.HeaderSize = sizeof(EFI_TCG2_EVENT_HEADER);
  event->Header.HeaderVersion = 1;
  event->Header.PCRIndex = pcr;
  event->Size = sizeof(*event) - sizeof(event->Event) + event_size;
  if (!is_pecoff)
    event->Header.EventType = EV_IPL;
  else {
    event->Header.EventType = EV_EFI_BOOT_SERVICES_APPLICATION;
    image_load = (EFI_IMAGE_LOAD_EVENT *) event->Event;
    image_load->ImageLocationInMemory = (grub_efi_physical_address_t) buf;
    image_load->ImageLengthInMemory = (grub_uint64_t) size;
grub_efi_modules_addr (void)
    image_load->ImageLinkTimeAddress = image->image_base; // pe_parse

260   file_path = make_file_path (dp, filename);
261   if (! file_path)
262     goto fail;
263 
264   grub_printf ("file path: ");
265   grub_efi_print_device_path (file_path);
    image_load->LengthOfDevicePath = FilePathSize;
    grub_memcpy (image_load->DevicePath, FilePath, FilePathSize);
  }

  status = efi_call_5 (tpm->hash_log_extend_event, tpm, flags, (grub_efi_physical_address_t) buf,
		       (grub_uint64_t) size, event);
  grub_free (event);

  switch (status) {
  case GRUB_EFI_SUCCESS:
    return 0;
  case GRUB_EFI_DEVICE_ERROR:
    return grub_error (GRUB_ERR_IO, N_("Command failed"));
  case GRUB_EFI_INVALID_PARAMETER:
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Invalid parameter"));
  case GRUB_EFI_BUFFER_TOO_SMALL:
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Output buffer too small"));
  case GRUB_EFI_NOT_FOUND:
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("TPM unavailable"));
  default:
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("Unknown TPM error"));
  }
}

grub_err_t
grub_tpm_log_event(unsigned char *buf, grub_size_t size, grub_uint8_t pcr,
		   const char *description)
{
  grub_efi_handle_t tpm_handle;
  grub_efi_uint8_t protocol_version;

  if (!grub_tpm_handle_find(&tpm_handle, &protocol_version))
    return 0;

  if (protocol_version == 1) {
    return grub_tpm1_log_event(tpm_handle, buf, size, pcr, description);
  } else {
    return grub_tpm2_log_event(tpm_handle, buf, size, pcr, description);
  }
}
