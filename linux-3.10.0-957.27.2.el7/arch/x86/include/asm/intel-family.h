#ifndef _ASM_X86_INTEL_FAMILY_H
#define _ASM_X86_INTEL_FAMILY_H

/*
 * "Big Core" Processors (Branded as Core, Xeon, etc...)
 *
 * The "_X" parts are generally the EP and EX Xeons, or the
 * "Extreme" ones, like Broadwell-E.
 *
 * Things ending in "2" are usually because we have no better
 * name for them.  There's no processor called "WESTMERE2".
 *
 * While adding a new CPUID for a new microarchitecture, add a new
 * group to keep logically sorted out in chronological order. Within
 * that group keep the CPUID for the variants sorted by model number.
 */

#define INTEL_FAM6_CORE_YONAH		0x0E
#define INTEL_FAM6_CORE2_MEROM		0x0F
#define INTEL_FAM6_CORE2_MEROM_L	0x16
#define INTEL_FAM6_CORE2_PENRYN		0x17
#define INTEL_FAM6_CORE2_DUNNINGTON	0x1D

#define INTEL_FAM6_NEHALEM		0x1E
#define INTEL_FAM6_NEHALEM_G		0x1F /* Auburndale / Havendale */
#define INTEL_FAM6_NEHALEM_EP		0x1A
#define INTEL_FAM6_NEHALEM_EX		0x2E
#define INTEL_FAM6_WESTMERE		0x25
#define INTEL_FAM6_WESTMERE_EP		0x2C
#define INTEL_FAM6_WESTMERE_EX		0x2F

#define INTEL_FAM6_SANDYBRIDGE		0x2A
#define INTEL_FAM6_SANDYBRIDGE_X	0x2D
#define INTEL_FAM6_IVYBRIDGE		0x3A
#define INTEL_FAM6_IVYBRIDGE_X		0x3E

#define INTEL_FAM6_HASWELL_CORE		0x3C
#define INTEL_FAM6_HASWELL_X		0x3F
#define INTEL_FAM6_HASWELL_ULT		0x45
#define INTEL_FAM6_HASWELL_GT3E		0x46

#define INTEL_FAM6_BROADWELL_CORE	0x3D
#define INTEL_FAM6_BROADWELL_XEON_D	0x56
#define INTEL_FAM6_BROADWELL_GT3E	0x47
#define INTEL_FAM6_BROADWELL_X		0x4F

#define INTEL_FAM6_SKYLAKE_MOBILE	0x4E
#define INTEL_FAM6_SKYLAKE_DESKTOP	0x5E
#define INTEL_FAM6_SKYLAKE_X		0x55
#define INTEL_FAM6_KABYLAKE_MOBILE	0x8E
#define INTEL_FAM6_KABYLAKE_DESKTOP	0x9E

#define INTEL_FAM6_CANNONLAKE_MOBILE	0x66

/* "Small Core" Processors (Atom) */

#define INTEL_FAM6_ATOM_BONNELL	0x1C
#define INTEL_FAM6_ATOM_BONNELL_MID	0x26
#define INTEL_FAM6_ATOM_SALTWELL_MID		0x27
#define INTEL_FAM6_ATOM_SALTWELL_TABLET	0x35
#define INTEL_FAM6_ATOM_SALTWELL	0x36
#define INTEL_FAM6_ATOM_SILVERMONT	0x37 /* BayTrail/BYT / Valleyview */
#define INTEL_FAM6_ATOM_SILVERMONT_X	0x4D /* Avaton/Rangely */
#define INTEL_FAM6_ATOM_AIRMONT		0x4C /* CherryTrail / Braswell */
#define INTEL_FAM6_ATOM_SILVERMONT_MID	0x4A /* Tangier */
#define INTEL_FAM6_ATOM_AIRMONT_MID	0x5A /* Annidale */
#define INTEL_FAM6_ATOM_GOLDMONT	0x5C
#define INTEL_FAM6_ATOM_GOLDMONT_PLUS	0x7A
#define INTEL_FAM6_ATOM_GOLDMONT_X	0x5F /* Goldmont Microserver */

/* Xeon Phi */

#define INTEL_FAM6_XEON_PHI_KNL		0x57 /* Knights Landing */
#define INTEL_FAM6_XEON_PHI_KNM		0x85 /* Knights Mill */

#endif /* _ASM_X86_INTEL_FAMILY_H */
