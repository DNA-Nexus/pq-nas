# Internal Red Team Playbook

**Version:** 1.0
**Created:** 2026-06-02
**Status:** Template -- adapt to your project before use
**Classification:** Internal Use Only

---

## Table of Contents

1. [Purpose](#1-purpose)
2. [Scope of This Playbook](#2-scope-of-this-playbook)
3. [Roles and Responsibilities](#3-roles-and-responsibilities)
4. [Rules of Engagement](#4-rules-of-engagement)
5. [Activity Safety Levels](#5-activity-safety-levels)
6. [Forbidden Activities](#6-forbidden-activities)
7. [Approval Gates](#7-approval-gates)
8. [Severity Model](#8-severity-model)
9. [Report Template](#9-report-template)
10. [Evidence Handling](#10-evidence-handling)
11. [Retest Process](#11-retest-process)
12. [AI-Agent Safety Rules](#12-ai-agent-safety-rules)
13. [Checklist A -- Read-Only Code Review](#13-checklist-a----read-only-code-review)
14. [Checklist B -- Safe Local Testing](#14-checklist-b----safe-local-testing)
15. [Checklist C -- Test-Environment Red-Team Work](#15-checklist-c----test-environment-red-team-work)
16. [Appendix A -- Glossary](#16-appendix-a----glossary)
17. [Appendix B -- References](#17-appendix-b----references)
18. [Appendix C -- Document History](#18-appendix-c----document-history)

---

## 1. Purpose

This playbook provides a repeatable, safe framework for a small software development
team to conduct internal red-team security assessments. It is designed to:

- Improve the security posture of internally developed software.
- Provide structured methodology so assessments are consistent and comparable.
- Prevent accidental damage to production systems, data, or availability.
- Ensure all testing is authorized, documented, and reviewable.
- Build security skills across the team over time.

This playbook is **not** a replacement for external professional penetration testing.
It is a complement -- an internal practice that raises the security baseline between
external assessments.

---

## 2. Scope of This Playbook

This playbook covers:

- Read-only source code review for security issues.
- Architecture and threat-model review.
- Dependency and configuration review.
- Authorized testing in local development environments.
- Authorized testing in dedicated, isolated test environments.
- Reporting, remediation tracking, and retesting.

This playbook does **not** cover:

- Testing against production systems (requires a separate, project-specific RoE
  approved by the system owner).
- Social engineering or physical security testing.
- Testing of third-party systems or services not owned by the team.
- Compliance auditing (PCI-DSS, SOC 2, HIPAA, etc.).

---

## 3. Roles and Responsibilities

A small team can run meaningful assessments with as few as three people.
Individuals may hold multiple roles, with the constraint noted below.

| Role | Responsibility | Minimum Staffing |
|------|---------------|-----------------|
| **Assessment Lead** | Plans the assessment, defines scope, writes the RoE, assigns tasks, owns the final report. | 1 required |
| **Red Team Operator** | Conducts the actual review/testing, documents findings with evidence. | 1 required |
| **System Owner** | Authorizes the assessment, provides architecture context, accepts or disputes findings, owns remediation. | 1 required |
| **Fix Implementer** | Develops and deploys remediations for confirmed findings. | 1 required |
| **Report Reviewer** | Reviews the report for accuracy, completeness, and severity calibration before distribution. | 1 required |
| **Retest Reviewer** | Verifies that fixes resolve findings without introducing regressions. | 1 required |

### Constraints

- The **Red Team Operator** and the **Fix Implementer** for the same finding must
  be different people (separation of duties -- the person who finds a bug should
  not be the sole person who verifies their own fix).
- The **System Owner** must sign off on the RoE before any testing begins.
- In a 3-person team, a practical split is:
  - Person A: Assessment Lead + Red Team Operator
  - Person B: System Owner + Fix Implementer
  - Person C: Report Reviewer + Retest Reviewer

### Role Rotation

Rotate roles between assessments so that all team members develop both offensive
and defensive perspectives. Track rotation in the assessment log.

---

## 4. Rules of Engagement

Every assessment requires a written Rules of Engagement (RoE) document, even for
routine internal reviews. The RoE may be brief for low-risk activities (code review)
and must be detailed for higher-risk activities (test-environment exploitation).

### 4.1 RoE Template

```
RULES OF ENGAGEMENT
====================

Assessment ID:    [e.g., RT-2026-003]
Assessment Title: [e.g., Authentication Module Security Review]
Date Range:       [Start date] to [End date]
Assessment Lead:  [Name]
System Owner:     [Name]
Approved By:      [System Owner signature/acknowledgment + date]

SCOPE
-----
In scope:
  - [List specific systems, repos, modules, endpoints, environments]

Out of scope:
  - [List excluded systems, environments, third-party services]

Environment:
  - [ ] Local development only
  - [ ] Dedicated test environment: [URL/identifier]
  - [ ] Staging: [URL/identifier] (requires System Owner approval)
  - [ ] Production: [URL/identifier] (requires System Owner + [additional] approval)

ACTIVITY LEVEL
--------------
  - [ ] Level 1: Read-only review (code, config, architecture)
  - [ ] Level 2: Local testing (operator's own machine, no network exposure)
  - [ ] Level 3: Test-environment testing (dedicated isolated environment)
  - [ ] Level 4: Staging/production testing (requires additional approval)

CONSTRAINTS
-----------
  - Maximum acceptable latency impact: [e.g., none / 100ms / N/A]
  - Testing hours: [e.g., any time / business hours only / off-hours only]
  - Data restrictions: [e.g., synthetic data only / no real user data]
  - Credential restrictions: [e.g., test accounts only / no brute-forcing]

FORBIDDEN ACTIONS
-----------------
  [See Section 6 of the playbook -- list any additional project-specific
   prohibitions here]

EMERGENCY PROCEDURES
--------------------
  - Abort trigger: [condition that stops all testing immediately]
  - Communication channel: [e.g., #security-channel, phone number]
  - Escalation contact: [Name + contact method]
  - If a critical vulnerability is found mid-test: [procedure]

SIGNATURES
----------
  Assessment Lead: _________________ Date: _________
  System Owner:    _________________ Date: _________
```

### 4.2 RoE Principles

1. **Written before testing begins.** No exceptions.
2. **Signed by the System Owner.** Even if that is a teammate -- the formality matters.
3. **Specific, not vague.** "The auth module" is insufficient. "The /api/auth/* endpoints
   in the user-service repository, tested against the local Docker environment" is specific.
4. **Conservative by default.** If the RoE does not explicitly permit an activity, it is
   not permitted.
5. **Revocable at any time.** The System Owner can halt the assessment at any point.
6. **Stored with the report.** The RoE is an appendix to the final report.

---

## 5. Activity Safety Levels

### Level 1 -- Read-Only Review (Low Risk)

| Aspect | Detail |
|--------|--------|
| **Activities** | Source code review, architecture review, threat modeling, dependency audit, configuration review, documentation review, secrets scanning of repos |
| **Environment** | No running systems required; operates on source code and documents only |
| **Approval** | Assessment Lead approval sufficient; System Owner informed |
| **Risk** | Negligible -- no systems are touched |
| **Data Handling** | No access to live data; findings reference code locations only |

### Level 2 -- Local Testing (Low-Medium Risk)

| Aspect | Detail |
|--------|--------|
| **Activities** | Running the application locally, manual testing of endpoints, fuzzing local instances, attempting local privilege escalation, testing auth flows, exploiting vulnerabilities in a local-only environment |
| **Environment** | Operator's own development machine; no network exposure; local databases with synthetic data only |
| **Approval** | Assessment Lead + System Owner approval required |
| **Risk** | Low -- isolated to operator's machine; risk of local data loss or misconfiguration |
| **Data Handling** | Synthetic/test data only; no production data on local machines during testing |

### Level 3 -- Test-Environment Testing (Medium Risk)

| Aspect | Detail |
|--------|--------|
| **Activities** | All Level 2 activities plus: network-level testing, multi-service interaction testing, persistence testing, lateral movement testing between test services, load/stress testing within defined limits |
| **Environment** | Dedicated, isolated test environment with no connection to production; synthetic data only; environment can be rebuilt from scratch |
| **Approval** | Full RoE signed by Assessment Lead + System Owner |
| **Risk** | Medium -- could disrupt test environment; could expose test credentials if environment is misconfigured |
| **Data Handling** | Synthetic data only; test environment must not contain production data, credentials, or secrets |

### Level 4 -- Staging/Production Testing (High Risk)

| Aspect | Detail |
|--------|--------|
| **Activities** | Carefully scoped testing against staging or production systems |
| **Environment** | Staging or production |
| **Approval** | Full RoE + System Owner + additional organizational approval (e.g., CTO, team lead, or equivalent) |
| **Risk** | High -- potential for user impact, data exposure, service disruption |
| **Data Handling** | Real data may be present; strict rules against exfiltration; findings must not include real user data |

**This playbook focuses on Levels 1-3.** Level 4 should be conducted only by
experienced practitioners or external professionals with a project-specific RoE.

---

## 6. Forbidden Activities

The following activities are **never permitted** under this playbook regardless
of approval level:

| # | Forbidden Activity | Reason |
|---|-------------------|--------|
| F1 | Testing systems not listed in the RoE | Legal and ethical boundary |
| F2 | Testing third-party services without their written consent | Legal liability |
| F3 | Exfiltrating, copying, or storing real user data | Privacy and legal compliance |
| F4 | Denial-of-service attacks against any shared or production system | Availability impact |
| F5 | Modifying production data, databases, or state | Data integrity |
| F6 | Modifying production security controls (firewalls, SELinux, sudoers, etc.) | Could compromise production security |
| F7 | Installing persistent backdoors, rootkits, or implants on any system not explicitly designated as a disposable test target | Persistence risk |
| F8 | Brute-force attacks against production authentication systems | Account lockout, availability |
| F9 | Social engineering of team members or users without explicit HR/legal approval | Ethical and legal concerns |
| F10 | Sharing findings, exploits, or evidence outside the authorized team | Information security |
| F11 | Continuing testing after the System Owner revokes authorization | Violation of consent |
| F12 | Using discovered credentials to access systems outside the RoE scope | Unauthorized access |
| F13 | Destroying or tampering with audit logs | Forensic integrity |

If an operator is unsure whether an activity is permitted, they must **stop and
ask the Assessment Lead** before proceeding.

---

## 7. Approval Gates

Approval gates are checkpoints that require explicit sign-off before the
assessment can proceed to the next phase.

```
GATE 0: Assessment Initiation
  Required: Assessment Lead drafts RoE
  Approver: System Owner
  Artifact: Signed RoE document
  Criteria: Scope, constraints, and emergency procedures are defined
            |
            v
GATE 1: Review Phase Complete
  Required: Read-only review findings documented
  Approver: Assessment Lead
  Artifact: Draft findings list
  Criteria: All Level 1 checklist items completed
            |
            v
GATE 2: Local Testing Authorization
  Required: Gate 1 complete; local testing plan documented
  Approver: System Owner reviews testing plan
  Artifact: Approved testing plan appended to RoE
  Criteria: Test data is synthetic; environment is isolated
            |
            v
GATE 3: Test-Environment Testing Authorization
  Required: Gate 2 complete; test environment verified as isolated
  Approver: System Owner confirms environment isolation
  Artifact: Environment isolation checklist completed
  Criteria: No production data; no production connectivity;
            environment is rebuildable
            |
            v
GATE 4: Report Finalization
  Required: All testing complete; draft report written
  Approver: Report Reviewer
  Artifact: Reviewed and finalized report
  Criteria: Findings are accurate, evidence is sufficient,
            severity ratings are calibrated
            |
            v
GATE 5: Remediation Verification
  Required: Fixes implemented for all Critical/High findings
  Approver: Retest Reviewer (not the Fix Implementer)
  Artifact: Retest results appended to report
  Criteria: Fixes resolve the finding; no regressions introduced
```

Each gate must be explicitly acknowledged (message, signature, or ticket
comment) before proceeding. Do not skip gates.

---

## 8. Severity Model

### Severity Definitions

| Severity | CVSS Range | Definition | Remediation SLA |
|----------|-----------|------------|----------------|
| **Critical** | 9.0 -- 10.0 | Exploitable remotely with no or minimal prerequisites. Leads to full system compromise, mass data breach, or complete loss of availability. Likely to be exploited in the wild. | Fix immediately; verify within 48 hours |
| **High** | 7.0 -- 8.9 | Exploitable with low complexity. Leads to significant data exposure, privilege escalation to admin, or major functionality bypass. | Fix within 1 week; verify within 2 weeks |
| **Medium** | 4.0 -- 6.9 | Exploitable with moderate prerequisites (authenticated access, specific configuration, user interaction). Leads to limited data exposure or partial control. | Fix within 1 month; verify within 6 weeks |
| **Low** | 0.1 -- 3.9 | Requires significant prerequisites, insider access, or unlikely conditions. Limited impact even if exploited. | Fix within 1 quarter; verify at next assessment |
| **Informational** | 0.0 | Best-practice deviation with no direct exploitability. Defense-in-depth improvement. Hardening recommendation. | Address at team's discretion; track for awareness |

### Severity Assessment Factors

When assigning severity, consider:

1. **Exploitability**: How easy is it to exploit? Remote vs. local? Authenticated vs. unauthenticated? Automated vs. manual?
2. **Impact**: What does the attacker gain? Data? Control? Availability disruption?
3. **Scope**: Is impact confined to one component or does it cross trust boundaries?
4. **Affected users/data**: How many users or how much data is affected?
5. **Detection**: Would exploitation be detected by current monitoring?
6. **Existing mitigations**: Are there compensating controls that reduce practical risk?

### Severity Disputes

If the Red Team Operator and System Owner disagree on severity:

1. Both parties document their rationale.
2. The Report Reviewer makes the final determination.
3. The dispute and resolution are recorded in the report.

---

## 9. Report Template

Each assessment produces a report following this structure. For small assessments
(Level 1 only), sections may be abbreviated but not omitted.

```
INTERNAL RED TEAM ASSESSMENT REPORT
=====================================

DOCUMENT CONTROL
  Report ID:        [e.g., RT-2026-003]
  Assessment Title: [e.g., Authentication Module Security Review]
  Version:          [e.g., 1.0 / 1.1 after retest]
  Date:             [Report date]
  Classification:   Internal Use Only
  Distribution:     [List of authorized recipients]

1. EXECUTIVE SUMMARY
   - Assessment objective (1-2 sentences)
   - Scope summary (1-2 sentences)
   - Key statistics:
       Critical: [N]   High: [N]   Medium: [N]   Low: [N]   Info: [N]
   - Top findings (plain language, max 5)
   - Overall risk assessment (1 paragraph)
   - Strategic recommendations (max 3)

2. ASSESSMENT DETAILS
   - Assessment ID and dates
   - Team members and roles
   - Activity level(s) conducted (Level 1/2/3/4)
   - Methodology and frameworks used
   - Tools used (with versions)
   - Limitations and constraints encountered
   - RoE reference (appended or linked)

3. SCOPE
   - Systems / modules / repos assessed
   - Systems / modules excluded
   - Environment(s) used
   - Access level provided

4. FINDINGS

   For each finding, use this format:

   FINDING [RT-2026-003-01]
   ========================
   Title:          [Descriptive title]
   Severity:       [Critical / High / Medium / Low / Info]
   CVSS Score:     [If applicable]
   CWE:            [e.g., CWE-89]
   OWASP Category: [e.g., A03:2021 Injection]
   Status:         [Open / Remediated / Accepted Risk / Disputed]
   Component:      [File, module, endpoint, or service affected]

   Description:
     [What the vulnerability is, in clear technical language]

   Evidence:
     [Code snippets, screenshots, log excerpts, request/response pairs.
      NEVER include real user data, production secrets, or
      information that could be used to exploit production systems.]

   Impact:
     [What an attacker could achieve by exploiting this vulnerability.
      Include the worst realistic scenario, not just the theoretical maximum.]

   Reproduction Steps:
     [Numbered steps to reproduce the finding in the test environment.
      Must be detailed enough for the Retest Reviewer to verify.]

     1. [Step 1]
     2. [Step 2]
     3. [Expected result vs. actual result]

   Recommendation:
     [Specific, actionable fix guidance. Include code examples if helpful.
      Suggest the minimal effective fix, not a complete redesign.]

   References:
     [Links to OWASP, CWE, CVE, documentation, or related findings]

   Remediation Notes:
     [Filled in after fix: what was changed, by whom, PR/commit reference]

   Retest Result:
     [Filled in after retest: Pass / Fail / Partial, date, reviewer name]

5. FINDINGS SUMMARY TABLE

   | ID | Title | Severity | Status | Component |
   |----|-------|----------|--------|-----------|
   |    |       |          |        |           |

6. REMEDIATION PRIORITIES
   - Ordered list of fixes by risk (Critical first)
   - Dependencies between fixes (if any)
   - Quick wins vs. longer-term items

7. POSITIVE OBSERVATIONS
   [Security controls that are working well. This section is important
    for team morale and to identify practices worth preserving.]

8. RESIDUAL RISK
   - Findings accepted as risk (with justification)
   - Areas not tested and why
   - Recommendations for the next assessment
   - Recommended reassessment timeline

9. APPENDICES
   A. Rules of Engagement (signed copy)
   B. Detailed tool output (sanitized)
   C. Environment configuration details
   D. Glossary of terms used
```

---

## 10. Evidence Handling

### Collection

- Evidence must be collected at the time of discovery. Do not rely on recreating
  it later.
- Acceptable evidence types: code snippets with file paths and line numbers,
  terminal output (sanitized), screenshots, HTTP request/response pairs, log
  excerpts, configuration file excerpts.
- Every piece of evidence must include a timestamp and the environment in which
  it was collected.

### Sanitization

Before including evidence in the report:

- **Remove** real user data (names, emails, IPs, etc.). Replace with placeholders
  like `[REDACTED]` or `user@example.com`.
- **Remove** production secrets, API keys, tokens, and passwords. Replace with
  `[REDACTED-SECRET]`. Note: the existence and location of the secret should
  still be documented -- just not the value.
- **Remove** internal hostnames or IPs that could aid an attacker if the report
  is leaked. Use generic labels like `[PROD-DB-HOST]` or `[TEST-SERVER-1]`.
- **Verify** that screenshots do not contain sensitive data in browser tabs, URL
  bars, notification popups, or terminal history.

### Storage

- Reports and evidence are stored in a designated, access-controlled location.
- Access is limited to the assessment team and authorized stakeholders.
- Define a retention period (recommended: retain for at least 2 assessment cycles
  to allow trend analysis, then archive or destroy per organizational policy).
- Evidence from test environments may be destroyed after the report is finalized
  and retesting is complete.

### Secrets Discovered During Testing

If real secrets (production passwords, API keys, tokens, private keys) are
discovered during testing:

1. **Do not copy, store, or transmit the secret value.** Note only the location
   and type.
2. **Report immediately** to the System Owner via the emergency communication
   channel defined in the RoE.
3. **The System Owner must rotate the secret** before the assessment continues.
4. **Document the finding** with location and type only, never the secret value.

---

## 11. Retest Process

### When to Retest

- All **Critical** and **High** findings must be retested after remediation.
- **Medium** findings should be retested; may be deferred to the next assessment
  cycle if resources are constrained.
- **Low** and **Informational** findings are retested at the next full assessment.

### Who Retests

- The **Retest Reviewer** must be someone other than the Fix Implementer.
- The original Red Team Operator is preferred (they understand the finding best)
  but not required.

### Retest Procedure

1. **Verify the fix is deployed** to the test environment. Confirm the specific
   commit/PR that addresses the finding.
2. **Reproduce the original attack path** using the steps documented in the finding.
3. **Verify the fix blocks the attack.** The expected behavior should now occur.
4. **Test for regressions.** Verify that the fix does not:
   - Break legitimate functionality.
   - Introduce a new vulnerability (e.g., a fix that adds input validation
     but uses a flawed regex).
   - Shift the vulnerability to a different location.
5. **Test for bypass.** Attempt reasonable variations of the original attack to
   ensure the fix is robust, not just a point fix for the exact reproduction
   steps.
6. **Document the result** in the finding's Retest Result field:
   - **Pass**: The fix fully resolves the finding and no bypass was found.
   - **Fail**: The original attack still works, or a trivial bypass exists.
   - **Partial**: The fix reduces severity but does not fully resolve the finding.
     Update the severity rating accordingly.

### Retest Report Update

After retesting, update the main report:

- Update each finding's Status and Retest Result fields.
- Update the Findings Summary Table.
- Increment the report version number.
- Add a retest summary to the Executive Summary.

---

## 12. AI-Agent Safety Rules

When using AI coding agents (including but not limited to Claude Code, Copilot,
or similar tools) during red-team assessments, the following rules apply:

### General Principles

1. **AI agents are tools, not autonomous operators.** A human team member must
   review and authorize every action that modifies systems, files, or
   configurations.
2. **AI agents are bound by the same RoE as human operators.** The RoE does not
   expand because a tool is executing the action instead of a person.
3. **AI agents must not be given production credentials.** Provide test/synthetic
   credentials only, scoped to the test environment.

### Permitted AI-Agent Activities

- Reading and analyzing source code (Level 1).
- Searching for patterns (hardcoded secrets, vulnerable code patterns, misconfigurations).
- Generating security review checklists and reports.
- Explaining code behavior and identifying potential vulnerabilities.
- Drafting remediation recommendations.
- Running automated scans in local or test environments (Level 2-3) when the
  operator reviews the commands before execution.

### Restricted AI-Agent Activities (Require Human Review Before Execution)

- Running any command that modifies files, configurations, or system state.
- Executing network requests against any system (even test environments).
- Installing packages or dependencies.
- Running scripts that interact with databases.
- Any action that could have side effects beyond the operator's local environment.

### Forbidden AI-Agent Activities

- Autonomous execution against production systems.
- Storing, logging, or transmitting real secrets or user data.
- Making decisions about severity ratings without human review.
- Generating functional exploit code intended for use against non-test systems.
- Bypassing or disabling safety controls, sandboxes, or permission boundaries.
- Executing denial-of-service patterns, even in test environments, without
  explicit operator instruction and RoE authorization.

### AI-Agent Session Hygiene

- Review the AI agent's context/history at the start of each session to ensure
  no sensitive data from previous sessions has leaked.
- Do not paste production secrets, real user data, or sensitive internal URLs
  into AI agent prompts.
- Treat AI agent conversation logs as assessment artifacts -- store and protect
  them accordingly.
- Clear or reset AI agent sessions after the assessment to prevent data retention.

---

## 13. Checklist A -- Read-Only Code Review

Use this checklist for Level 1 (read-only) security reviews. No running systems
are required.

### Pre-Review

- [ ] RoE is documented (may be abbreviated for Level 1).
- [ ] Scope is defined: which repos, branches, modules, and file paths are included.
- [ ] System Owner is informed that a review is starting.
- [ ] Reviewer has read access to the codebase.
- [ ] Architecture documentation (if any) has been reviewed.

### Authentication and Authorization

- [ ] Authentication mechanisms are identified (how do users prove identity?).
- [ ] Authorization checks are present at every access point (API endpoint, UI
      route, CLI command, file operation).
- [ ] Authorization is enforced server-side, not only client-side.
- [ ] Default-deny is used (access is blocked unless explicitly granted).
- [ ] Session management is reviewed: creation, expiration, invalidation, storage.
- [ ] Password/credential handling: hashing algorithm, salt usage, no plaintext storage.
- [ ] Multi-factor authentication is present where appropriate.
- [ ] API keys/tokens have appropriate scope, expiration, and rotation mechanisms.

### Input Validation and Injection

- [ ] All user input is validated before use (type, length, format, range).
- [ ] Parameterized queries are used for database access (no string concatenation).
- [ ] Command execution does not incorporate unsanitized user input.
- [ ] File paths constructed from user input are validated against path traversal.
- [ ] Template rendering uses auto-escaping (XSS prevention).
- [ ] XML parsing disables external entity resolution (XXE prevention).
- [ ] Deserialization of untrusted data is avoided or uses safe methods.
- [ ] Regular expressions are reviewed for ReDoS (catastrophic backtracking).

### Cryptography and Secrets

- [ ] No hardcoded secrets, API keys, passwords, or private keys in source code.
- [ ] Secrets are loaded from environment variables, vaults, or secure config -- not files committed to the repo.
- [ ] `.gitignore` (or equivalent) excludes secret files, `.env`, key files, etc.
- [ ] Cryptographic algorithms are current (no MD5 for security, no SHA1 for signatures, no DES/3DES, no RC4).
- [ ] TLS is enforced for all network communication.
- [ ] Certificate validation is not disabled or bypassed.
- [ ] Random number generation uses cryptographically secure sources (not `Math.random()` or `rand()`).
- [ ] Key sizes meet current recommendations (RSA >= 2048, AES >= 128, etc.).

### Data Handling

- [ ] Sensitive data at rest is encrypted.
- [ ] Sensitive data in transit is encrypted (TLS).
- [ ] Logging does not include secrets, tokens, passwords, or PII.
- [ ] Error messages do not leak internal details (stack traces, SQL errors, file paths).
- [ ] Temporary files containing sensitive data are cleaned up.
- [ ] Data retention and deletion are handled appropriately.

### Dependencies

- [ ] Third-party dependencies are inventoried.
- [ ] Known vulnerabilities in dependencies are checked (`npm audit`, `pip-audit`,
      `cargo audit`, or equivalent).
- [ ] Dependencies are pinned to specific versions (not floating ranges for
      security-critical packages).
- [ ] No unnecessary dependencies are included (smaller attack surface).
- [ ] Dependency sources are trusted (official registries, verified publishers).

### Configuration and Deployment

- [ ] Debug mode is disabled in production configurations.
- [ ] Default credentials are changed or disabled.
- [ ] Unnecessary services, ports, and features are disabled.
- [ ] File permissions follow least privilege.
- [ ] CORS policy is restrictive (not `*` for authenticated endpoints).
- [ ] Security headers are configured (CSP, HSTS, X-Frame-Options, etc.).
- [ ] Environment-specific configuration is separated from code.

### Error Handling and Logging

- [ ] Errors are caught and handled; no unhandled exceptions reach users.
- [ ] Error responses do not leak sensitive information.
- [ ] Security-relevant events are logged (login attempts, access denied,
      privilege changes, data modifications).
- [ ] Logs include sufficient context (timestamp, user, action, resource, result).
- [ ] Log injection is prevented (user input is not written directly to logs without sanitization).
- [ ] Logs are stored securely and are tamper-resistant.

### Business Logic

- [ ] Rate limiting is implemented for sensitive operations (login, password reset, API calls).
- [ ] Race conditions are considered for concurrent operations.
- [ ] State transitions are validated (e.g., order status can't skip steps).
- [ ] Numeric operations check for overflow/underflow where relevant.
- [ ] File upload validates type, size, and content (not just extension).

### Post-Review

- [ ] Findings are documented with file paths, line numbers, and descriptions.
- [ ] Severity is assigned to each finding.
- [ ] Report is drafted and submitted for review.

---

## 14. Checklist B -- Safe Local Testing

Use this checklist for Level 2 (local testing) security work. All testing occurs
on the operator's own machine with no network exposure.

### Pre-Testing

- [ ] Level 1 review (Checklist A) is complete for the components being tested.
- [ ] RoE is signed by Assessment Lead and System Owner.
- [ ] Local test environment is set up and verified:
  - [ ] Application runs locally (Docker, local server, etc.).
  - [ ] Database uses synthetic/test data only -- no production data.
  - [ ] No connections to production services or APIs.
  - [ ] Network listeners are bound to localhost only (127.0.0.1 / ::1).
- [ ] Test accounts are created with various privilege levels (admin, regular
      user, unauthenticated).
- [ ] Operator's machine has no production credentials in active sessions,
      environment variables, or agent contexts.

### Authentication Testing

- [ ] Attempt login with invalid credentials -- verify proper rejection and messaging.
- [ ] Attempt login with empty/null credentials.
- [ ] Test session expiration -- does the session actually become invalid?
- [ ] Test session fixation -- can an attacker set a user's session ID?
- [ ] Test concurrent sessions -- can the same account have multiple sessions?
      Is that intended?
- [ ] Test password reset flow -- is the token single-use, time-limited, and
      unpredictable?
- [ ] Test remember-me functionality -- is the token secure?

### Authorization Testing

- [ ] Access each endpoint/function as each role -- verify access controls.
- [ ] Attempt to access admin functions as a regular user.
- [ ] Attempt to access another user's resources (IDOR testing).
- [ ] Attempt to perform actions after logout/session expiration.
- [ ] Test API endpoints directly (bypass UI-only controls).
- [ ] Test for parameter tampering (modify user IDs, role fields, etc. in requests).

### Input Testing

- [ ] Test each input field with common injection payloads (SQL, XSS, command
      injection) -- use well-known test strings, not novel exploits.
- [ ] Test file upload with malicious file types (if upload exists).
- [ ] Test path traversal in file access parameters.
- [ ] Test with oversized inputs (buffer overflow / DoS at the application level).
- [ ] Test with unexpected types (string where integer expected, etc.).
- [ ] Test with Unicode, null bytes, and special characters.

### API Testing

- [ ] Test each API endpoint with no authentication.
- [ ] Test each API endpoint with invalid/expired tokens.
- [ ] Test with malformed requests (missing fields, extra fields, wrong types).
- [ ] Test rate limiting -- is it enforced?
- [ ] Test CORS -- can a cross-origin request access authenticated endpoints?

### Local Privilege and Configuration

- [ ] Check what the application process can access on the local filesystem.
- [ ] Check if the application runs with excessive privileges.
- [ ] Check if temporary files are created securely.
- [ ] Check if sensitive data is written to local logs.
- [ ] Check if debug/development features are accessible.

### Post-Testing

- [ ] All findings are documented with reproduction steps.
- [ ] Local test environment is torn down or reset.
- [ ] Any test artifacts (logs, screenshots, exports) are stored securely.
- [ ] No production-like data remains on the operator's machine.

---

## 15. Checklist C -- Test-Environment Red-Team Work

Use this checklist for Level 3 (test-environment) security work. Testing occurs
against a dedicated, isolated environment that mirrors production architecture.

### Pre-Testing

- [ ] Level 1 and Level 2 work (Checklists A and B) are complete.
- [ ] Full RoE is signed and distributed.
- [ ] Test environment is verified as isolated:
  - [ ] No network connectivity to production systems.
  - [ ] No shared databases or storage with production.
  - [ ] No shared credentials with production.
  - [ ] No real user data -- synthetic data only.
  - [ ] Environment can be rebuilt from scratch if damaged.
- [ ] Monitoring/logging is active in the test environment (to test detection).
- [ ] Emergency communication channel is established and tested.
- [ ] Backup/snapshot of the test environment is taken before testing begins.

### Network and Infrastructure Testing

- [ ] Port scan the test environment -- are only expected ports open?
- [ ] Service enumeration -- are services identified and versions known?
- [ ] TLS configuration -- are protocols and ciphers appropriate?
- [ ] DNS configuration -- any zone transfer or information disclosure?
- [ ] Firewall rules -- is segmentation working as intended?
- [ ] Container/VM configuration -- are isolation boundaries intact?

### Multi-Service Interaction Testing

- [ ] Test trust relationships between services -- does Service A properly
      authenticate to Service B?
- [ ] Test internal APIs -- do they validate authorization or assume trust?
- [ ] Test message queues/event buses -- can messages be injected or tampered?
- [ ] Test shared storage -- can one service access another's data?

### Privilege Escalation Testing

- [ ] From each access level, attempt to escalate privileges.
- [ ] Check for sudo misconfigurations in the test environment.
- [ ] Check for SUID/SGID binaries that could be leveraged.
- [ ] Check for writable cron jobs or systemd services.
- [ ] Check for kernel or container escape opportunities (note: do not actually
      exploit kernel vulnerabilities -- document and report).

### Persistence Testing

- [ ] After gaining access, identify methods an attacker could use to persist.
- [ ] Check if unauthorized cron jobs / scheduled tasks could be created.
- [ ] Check if authorized_keys or similar trust files are writable.
- [ ] Check if startup scripts are modifiable.
- [ ] Document persistence opportunities; only implement them if explicitly
      authorized in the RoE and the environment is disposable.

### Lateral Movement Testing

- [ ] From a compromised test service, what other services are reachable?
- [ ] Are credentials shared between services?
- [ ] Can service accounts access resources beyond their intended scope?
- [ ] Is network segmentation enforced between tiers (web, app, database)?

### Data Exposure Testing

- [ ] From each access level, what data is readable?
- [ ] Are database backups accessible?
- [ ] Are log files accessible and do they contain sensitive data?
- [ ] Are configuration files with secrets accessible?
- [ ] Can data be exfiltrated through unmonitored channels (DNS, ICMP, etc.)?

### Detection and Response Testing

- [ ] Were the testing activities logged?
- [ ] Were any alerts triggered?
- [ ] Would the blue team have noticed the activity?
- [ ] Are logs tamper-resistant (can the attacker delete their traces)?
- [ ] Is there sufficient forensic data to reconstruct the attack?

### Post-Testing

- [ ] All findings are documented with full reproduction steps and evidence.
- [ ] Any changes made to the test environment are documented.
- [ ] Test environment is restored to its pre-test state (or rebuilt).
- [ ] All test credentials are rotated or destroyed.
- [ ] All test artifacts are collected and stored securely.
- [ ] Assessment Lead confirms all testing is complete.
- [ ] Report is drafted and submitted to Report Reviewer (Gate 4).

---

## 16. Appendix A -- Glossary

| Term | Definition |
|------|-----------|
| **Attack surface** | The sum of all points where an attacker can attempt to enter or extract data from a system. |
| **Black-box testing** | Testing without prior knowledge of the system's internals. |
| **Blue team** | The defensive team responsible for detecting and responding to attacks. |
| **CVE** | Common Vulnerabilities and Exposures -- a public catalog of known vulnerabilities. |
| **CVSS** | Common Vulnerability Scoring System -- a standardized method for rating vulnerability severity. |
| **CWE** | Common Weakness Enumeration -- a categorization of software and hardware weakness types. |
| **DAST** | Dynamic Application Security Testing -- testing a running application for vulnerabilities. |
| **Gray-box testing** | Testing with partial knowledge of the system (e.g., access to source code but not architecture docs, or vice versa). |
| **IDOR** | Insecure Direct Object Reference -- a vulnerability where an attacker can access resources by manipulating identifiers. |
| **Lateral movement** | Moving from one compromised system to another within a network. |
| **Persistence** | An attacker's ability to maintain access to a system after initial compromise, surviving reboots or credential changes. |
| **Privilege escalation** | Gaining higher-level permissions than originally granted (vertical) or accessing another user's resources at the same level (horizontal). |
| **Purple team** | A collaborative exercise where red and blue teams work together in real time to improve both attack and defense capabilities. |
| **Red team** | The offensive team that simulates adversary behavior to test defenses. |
| **RoE** | Rules of Engagement -- the formal document defining the scope, constraints, and procedures for an assessment. |
| **SAST** | Static Application Security Testing -- analyzing source code for vulnerabilities without executing it. |
| **SBOM** | Software Bill of Materials -- an inventory of all components in a software product. |
| **White-box testing** | Testing with full knowledge of the system's internals (source code, architecture, credentials). |

---

## 17. Appendix B -- References

### Frameworks and Standards

- OWASP Top 10: https://owasp.org/www-project-top-ten/
- OWASP ASVS: https://owasp.org/www-project-application-security-verification-standard/
- OWASP Testing Guide: https://owasp.org/www-project-web-security-testing-guide/
- MITRE ATT&CK: https://attack.mitre.org/
- CWE: https://cwe.mitre.org/
- CVSS: https://www.first.org/cvss/

### Hardening Guides

- CIS Benchmarks: https://www.cisecurity.org/cis-benchmarks
- NIST SP 800-53: https://csrc.nist.gov/publications/detail/sp/800-53/rev-5/final

### Threat Modeling

- STRIDE: https://learn.microsoft.com/en-us/azure/security/develop/threat-modeling-tool-threats
- OWASP Threat Modeling: https://owasp.org/www-community/Threat_Modeling

---

## 18. Appendix C -- Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-06-02 | [Author] | Initial template |
